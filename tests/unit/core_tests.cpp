#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "3dsmsc/audio/aac_decoder.hpp"
#include "3dsmsc/audio/flac_decoder.hpp"
#include "3dsmsc/audio/mp3_decoder.hpp"
#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/config/settings_file.hpp"
#include "3dsmsc/library/artwork.hpp"
#include "3dsmsc/library/browse_index.hpp"
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/folder_browser.hpp"
#include "3dsmsc/library/folder_list_file.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/library/scanner.hpp"
#include "3dsmsc/library/track.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/queue/queue_file.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"

namespace {

class FakeFileSystem final : public threedsmsc::FileSystem {
 public:
  bool list_directory(const std::string& path, std::vector<threedsmsc::DirectoryEntry>& entries,
                      std::string& error) override {
    ++list_calls;
    const auto iterator = directories.find(path);
    if (iterator == directories.end()) {
      error = "missing directory";
      return false;
    }
    entries = iterator->second;
    error.clear();
    return true;
  }

  std::map<std::string, std::vector<threedsmsc::DirectoryEntry>> directories;
  int list_calls = 0;
};

threedsmsc::DirectoryEntry entry(const char* name, bool is_directory) {
  return {name, is_directory};
}

struct VisitContext {
  std::size_t count = 0;
};

void visit_track(void* context, const threedsmsc::Track&) {
  ++static_cast<VisitContext*>(context)->count;
}

bool cancel_scan(void* context) {
  return *static_cast<bool*>(context);
}

threedsmsc::Track make_track(const char* path, const char* title, const char* artist) {
  threedsmsc::Track track;
  track.path = path;
  track.title = title;
  track.artist = artist;
  track.format = threedsmsc::format_from_path(path);
  return track;
}

void test_defaults() {
  const threedsmsc::Settings settings = threedsmsc::default_settings();
  assert(settings.music_root == "sdmc:/3dsmsc/music/");
  assert(settings.volume == 0.8);
  assert(settings.search_case_sensitive == false);
  assert(settings.background_playback == true);
}

void test_settings_loading() {
  std::istringstream input(
      "[library]\n"
      "music_root = \"sdmc:/custom/music/\"\n"
      "scan_scope = \"selected\"\n"
      "[player]\n"
      "volume = 2.0\n"
      "search_case_sensitive = true\n");
  threedsmsc::Settings settings;
  std::string error;
  const bool loaded = threedsmsc::load_settings(input, settings, &error);
  assert(loaded);
  assert(settings.music_root == "sdmc:/custom/music/");
  assert(settings.scan_scope == threedsmsc::ScanScope::Selected);
  assert(settings.volume == 1.0);
  assert(settings.search_case_sensitive);
}

void test_invalid_settings_fall_back() {
  std::istringstream input("[player]\nvolume = high\n");
  threedsmsc::Settings settings;
  settings.volume = 0.25;
  std::string error;
  assert(!threedsmsc::load_settings(input, settings, &error));
  assert(!error.empty());
  assert(settings.music_root == "sdmc:/3dsmsc/music/");
  assert(settings.volume == 0.8);
}

void test_settings_file() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-config.toml";
  std::ofstream output(path);
  output << "[library]\n"
         << "music_root = \"/custom/music/\"\n";
  output.close();
  threedsmsc::Settings settings;
  std::string error;
  assert(threedsmsc::load_settings_file(path.string(), settings, &error));
  assert(settings.music_root == "/custom/music/");
  settings.volume = 0.35;
  settings.animation_enabled = false;
  assert(settings.theme == threedsmsc::Theme::Dark);
  settings.theme = threedsmsc::Theme::Light;
  assert(settings.seek_seconds == 10);
  settings.seek_seconds = 25;
  settings.search_case_sensitive = true;
  settings.next_button = "X2";
  assert(threedsmsc::save_settings_file(path.string(), settings, &error));
  assert(threedsmsc::load_settings_file(path.string(), settings, &error));
  assert(settings.volume == 0.35);
  assert(!settings.animation_enabled);
  assert(settings.theme == threedsmsc::Theme::Light);
  assert(settings.seek_seconds == 25);
  assert(settings.search_case_sensitive);
  assert(settings.next_button == "X2");
  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
}

void test_formats_and_search() {
  assert(threedsmsc::is_supported_path("Music/Track.MP3"));
  assert(threedsmsc::is_supported_path("Music/Track.m4a"));
  assert(!threedsmsc::is_supported_path("Music/notes.txt"));

  threedsmsc::LibraryIndex library;
  library.tracks = {
      make_track("sdmc:/music/Bold.flac", "Blue Sky", "Nova"),
      make_track("sdmc:/music/Quiet.mp3", "Quiet", "Nova"),
  };

  const auto insensitive = threedsmsc::find_tracks(library, "blue", false);
  assert(insensitive.size() == 1);
  assert(insensitive[0] == 0);
  const auto sensitive = threedsmsc::find_tracks(library, "blue", true);
  assert(sensitive.empty());

  library.tracks[0].album = "Blue";
  library.tracks[1].album = "Blue";
  const auto albums = threedsmsc::find_albums(library);
  assert(albums.size() == 1);
  assert(albums[0] == 0);
}

void test_scanner() {
  FakeFileSystem filesystem;
  filesystem.directories["/music"] = {
      entry("notes.txt", false), entry(".hidden.mp3", false), entry("Album", true),
      entry("a.mp3", false),     entry("b.FLAC", false),
  };
  filesystem.directories["/music/Album"] = {
      entry("c.m4a", false),
      entry("cover.png", false),
  };

  threedsmsc::FilenameMetadataReader metadata_reader;
  threedsmsc::ArtworkLocator artwork_locator(filesystem);
  threedsmsc::LibraryScanner scanner(filesystem, {true, 100}, &metadata_reader, &artwork_locator);
  threedsmsc::LibraryIndex library;
  VisitContext visit_context;
  std::string error;
  const threedsmsc::ScanCallbacks callbacks{&visit_context, visit_track, nullptr};
  assert(scanner.scan("/music", library, callbacks, &error));
  assert(library.state == threedsmsc::ScanState::Ready);
  assert(library.tracks.size() == 3);
  assert(visit_context.count == 3);
  assert(library.tracks[0].path == "/music/Album/c.m4a");
  assert(library.tracks[0].title == "c");
  assert(library.tracks[0].album == "Album");
  assert(library.tracks[0].artwork_path == "/music/Album/cover.png");
  assert(library.tracks[1].path == "/music/a.mp3");
  assert(library.tracks[2].path == "/music/b.FLAC");
  assert(error.empty());
}

void test_scanner_empty_and_cancelled() {
  FakeFileSystem filesystem;
  filesystem.directories["/empty"] = {entry("readme.txt", false)};
  threedsmsc::FilenameMetadataReader metadata_reader;
  threedsmsc::ArtworkLocator artwork_locator(filesystem);
  threedsmsc::LibraryScanner scanner(filesystem, {true, 100}, &metadata_reader, &artwork_locator);
  threedsmsc::LibraryIndex library;
  std::string error;
  assert(scanner.scan("/empty", library, {}, &error));
  assert(library.state == threedsmsc::ScanState::Ready);
  assert(library.tracks.empty());
  assert(library.message == "Go to Settings > Library > Full scan again");

  bool cancel = true;
  const threedsmsc::ScanCallbacks callbacks{&cancel, nullptr, cancel_scan};
  assert(!scanner.scan("/empty", library, callbacks, &error));
  assert(library.state == threedsmsc::ScanState::Cancelled);
  assert(library.message == "Scan cancelled");
}

void test_local_filesystem() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "3dsmsc-scanner-test";
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root / "Album", cleanup_error);
  assert(!cleanup_error);
  std::ofstream(root / "track.mp3") << "fixture";
  std::ofstream(root / "Album" / "track.flac") << "fixture";

  threedsmsc::LocalFileSystem filesystem;
  threedsmsc::FilenameMetadataReader metadata_reader;
  threedsmsc::ArtworkLocator artwork_locator(filesystem);
  threedsmsc::LibraryScanner scanner(filesystem, {true, 100}, &metadata_reader, &artwork_locator);
  threedsmsc::LibraryIndex library;
  std::string error;
  assert(scanner.scan(root.string(), library, {}, &error));
  assert(library.tracks.size() == 2);
  std::filesystem::remove_all(root, cleanup_error);
}

void test_artwork_priority() {
  FakeFileSystem filesystem;
  filesystem.directories["/album"] = {
      entry("folder.png", false),
      entry("cover.jpg", false),
      entry("cover.png", false),
  };
  threedsmsc::ArtworkLocator artwork_locator(filesystem);
  std::string artwork_path;
  assert(artwork_locator.find("/album/track.mp3", artwork_path));
  assert(artwork_path == "/album/cover.png");

  // Tracks of one folder share a single directory listing.
  filesystem.list_calls = 0;
  for (const char* track : {"/album/a.mp3", "/album/b.mp3", "/album/c.mp3"}) {
    assert(artwork_locator.find(track, artwork_path));
    assert(artwork_path == "/album/cover.png");
  }
  assert(filesystem.list_calls == 0);  // already cached from the first lookup
  filesystem.directories["/other"] = {entry("note.txt", false)};
  assert(!artwork_locator.find("/other/a.mp3", artwork_path));
  assert(!artwork_locator.find("/other/b.mp3", artwork_path));
  assert(filesystem.list_calls == 1);
}

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
  assert(metadata.duration_ms >= 1900);
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
  frames += id3_frame("TPE1", std::string("\x02\x00" "A\x00\xE9", 5));
  frames += id3_frame("TALB", std::string("\x01\xFF\xFE" "A\x00\x3D\xD8\x00\xDE", 9));
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
  xing_frame += std::string(32, '\0');                // side information
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

void test_browse_index() {
  threedsmsc::LibraryIndex library{};
  const auto add = [&](const char* title, const char* artist, const char* album,
                       std::uint16_t disc, std::uint16_t number) {
    threedsmsc::Track track{};
    track.title = title;
    track.artist = artist;
    track.album = album;
    track.disc_number = disc;
    track.track_number = number;
    library.tracks.push_back(track);
  };
  add("B side", "abba", "Arrival", 0, 2);
  add("Untagged", "ABBA", "Arrival", 0, 0);
  add("A side", "ABBA", "Arrival", 0, 1);
  add("Solo", "", "", 0, 0);
  add("Other", "Zed", "Arrival", 0, 1);
  const threedsmsc::BrowseIndex index = threedsmsc::build_browse_index(library);
  assert(index.artists.size() == 3);
  assert(index.artists[0].name == "abba" || index.artists[0].name == "ABBA");
  assert(index.artists[0].album_count == 1);
  assert(index.artists[1].name == "Unknown artist");
  assert(index.artists[2].name == "Zed");
  assert(index.albums.size() == 3);
  const auto& arrival = index.albums[0];
  assert(arrival.tracks.size() == 3);
  assert(library.tracks[arrival.tracks[0]].title == "A side");
  assert(library.tracks[arrival.tracks[1]].title == "B side");
  assert(library.tracks[arrival.tracks[2]].title == "Untagged");
  assert(threedsmsc::build_browse_index(threedsmsc::LibraryIndex{}).artists.empty());
}

void test_browse_navigator() {
  assert(threedsmsc::list_window_start(3, 2, 5) == 0);
  assert(threedsmsc::list_window_start(100, 0, 5) == 0);
  assert(threedsmsc::list_window_start(100, 50, 5) == 48);
  assert(threedsmsc::list_window_start(100, 99, 5) == 95);

  threedsmsc::LibraryIndex library{};
  const auto add = [&](const char* title, const char* artist, const char* album, std::uint16_t n) {
    threedsmsc::Track track{};
    track.title = title;
    track.artist = artist;
    track.album = album;
    track.track_number = n;
    library.tracks.push_back(track);
  };
  add("Two", "Band", "First", 2);
  add("One", "Band", "First", 1);
  add("Solo", "Band", "Second", 1);
  add("Other", "Other", "Third", 1);
  const threedsmsc::BrowseIndex index = threedsmsc::build_browse_index(library);
  threedsmsc::BrowseNavigator navigator;
  navigator.reset(&library, &index);
  threedsmsc::BrowseSelection playback;
  assert(navigator.level() == threedsmsc::BrowseLevel::Artists);
  assert(navigator.count() == 2);
  navigator.move(-1);
  assert(navigator.selected() == 1);
  navigator.move(1);
  assert(navigator.selected() == 0);
  assert(!navigator.enter(playback));
  assert(navigator.heading() == "Band");
  assert(navigator.count() == 2);
  assert(navigator.label(0) == "First");
  assert(!navigator.enter(playback));
  assert(navigator.heading() == "First");
  assert(navigator.label(0) == "01. One");
  assert(navigator.label(1) == "02. Two");
  navigator.move(1);
  assert(navigator.enter(playback));
  assert(playback.start == 1);
  assert(playback.tracks.size() == 2);
  assert(library.tracks[playback.tracks[playback.start]].title == "Two");
  assert(navigator.back());
  assert(navigator.level() == threedsmsc::BrowseLevel::Albums);
  assert(navigator.back());
  assert(!navigator.back());
  assert(navigator.level() == threedsmsc::BrowseLevel::Artists);

  threedsmsc::BrowseNavigator empty;
  assert(empty.count() == 0);
  assert(!empty.enter(playback));
  empty.move(1);
}

void test_aac_decoder() {
  std::array<std::int16_t, 8192> output{};
  std::size_t written = 0;
  threedsmsc::PcmBlockInfo info;

  threedsmsc::Track raw_track;
  raw_track.path = "tests/fixtures/silence.aac";
  raw_track.format = threedsmsc::AudioFormat::Aac;
  threedsmsc::AacDecoder raw_decoder;
  assert(raw_decoder.open(raw_track));
  assert(raw_decoder.decode(output.data(), output.size(), written, info) ==
         threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);

  threedsmsc::Track m4a_track;
  m4a_track.path = "tests/fixtures/silence.m4a";
  m4a_track.format = threedsmsc::AudioFormat::M4a;
  threedsmsc::AacDecoder m4a_decoder;
  assert(m4a_decoder.open(m4a_track));
  assert(m4a_decoder.seek(1000));
  assert(m4a_decoder.decode(output.data(), output.size(), written, info) ==
         threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);
}

void test_flac_decoder() {
  threedsmsc::Track track;
  track.path = "tests/fixtures/silence.flac";
  track.format = threedsmsc::AudioFormat::Flac;
  threedsmsc::FlacDecoder decoder;
  assert(decoder.open(track));
  assert(decoder.seek(1000));
  std::array<std::int16_t, 2304> output{};
  std::size_t written = 0;
  threedsmsc::PcmBlockInfo info;
  const threedsmsc::DecodeResult result =
      decoder.decode(output.data(), output.size(), written, info);
  assert(result == threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);
}

// Decodes a whole file the way the player does and returns how the stream ended.
threedsmsc::DecodeResult decode_to_end(threedsmsc::AudioDecoder& decoder, std::size_t& frames) {
  std::array<std::int16_t, 8192> output{};
  frames = 0;
  while (true) {
    std::size_t written = 0;
    threedsmsc::PcmBlockInfo info{};
    const threedsmsc::DecodeResult result =
        decoder.decode(output.data(), output.size(), written, info);
    if (result != threedsmsc::DecodeResult::FrameReady)
      return result;
    ++frames;
    assert(frames < 100000);
  }
}

void test_decoders_play_to_the_end() {
  threedsmsc::Track mp3;
  mp3.path = "tests/fixtures/silence.mp3";
  mp3.format = threedsmsc::AudioFormat::Mp3;
  threedsmsc::Mp3Decoder mp3_decoder;
  assert(mp3_decoder.open(mp3));
  std::size_t frames = 0;
  assert(decode_to_end(mp3_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 20);
  {
    // The 2.04 second fixture must decode to ~2 seconds of stereo PCM, not half of it.
    threedsmsc::Mp3Decoder timed;
    assert(timed.open(mp3));
    std::array<std::int16_t, 8192> pcm{};
    std::uint64_t pcm_frames = 0;
    std::size_t written = 0;
    threedsmsc::PcmBlockInfo info{};
    while (timed.decode(pcm.data(), pcm.size(), written, info) ==
           threedsmsc::DecodeResult::FrameReady) {
      pcm_frames += written / info.channels;
    }
    const std::uint64_t milliseconds = pcm_frames * 1000 / 44100;
    assert(milliseconds > 1900 && milliseconds < 2200);
  }

  // Real songs are far larger than the 32 KB input window; a long file must not overflow it.
  const std::filesystem::path long_path =
      std::filesystem::temp_directory_path() / "3dsmsc-long.mp3";
  {
    std::ifstream source("tests/fixtures/silence.mp3", std::ios::binary);
    std::stringstream content;
    content << source.rdbuf();
    std::ofstream output(long_path, std::ios::binary);
    for (int copy = 0; copy < 8; ++copy)
      output << content.str();
  }
  threedsmsc::Track long_mp3;
  long_mp3.path = long_path.string();
  long_mp3.format = threedsmsc::AudioFormat::Mp3;
  threedsmsc::Mp3Decoder long_decoder;
  assert(long_decoder.open(long_mp3));
  std::size_t long_frames = 0;
  const threedsmsc::DecodeResult long_result = decode_to_end(long_decoder, long_frames);
  assert(long_result == threedsmsc::DecodeResult::EndOfStream);
  assert(long_frames >= 8 * frames - 8);
  {
    // Seeking deep into a long file lands near the target and leaves the stream decodable.
    threedsmsc::Mp3Decoder seeker;
    assert(seeker.open(long_mp3));
    assert(seeker.seek(10000));
    std::array<std::int16_t, 8192> pcm{};
    std::size_t written = 0;
    threedsmsc::PcmBlockInfo info{};
    std::uint64_t remaining_frames = 0;
    while (seeker.decode(pcm.data(), pcm.size(), written, info) ==
           threedsmsc::DecodeResult::FrameReady) {
      remaining_frames += written / info.channels;
    }
    const std::uint64_t remaining_ms = remaining_frames * 1000 / 44100;
    assert(remaining_ms > 5500 && remaining_ms < 6500);  // 8 x 2.04s = 16.3s total
  }
  std::error_code cleanup_error;
  std::filesystem::remove(long_path, cleanup_error);

  threedsmsc::Track flac;
  flac.path = "tests/fixtures/silence.flac";
  flac.format = threedsmsc::AudioFormat::Flac;
  threedsmsc::FlacDecoder flac_decoder;
  assert(flac_decoder.open(flac));
  assert(decode_to_end(flac_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 1);

  threedsmsc::Track aac;
  aac.path = "tests/fixtures/silence.aac";
  aac.format = threedsmsc::AudioFormat::Aac;
  threedsmsc::AacDecoder aac_decoder;
  assert(aac_decoder.open(aac));
  assert(decode_to_end(aac_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 1);

  threedsmsc::Track m4a;
  m4a.path = "tests/fixtures/silence.m4a";
  m4a.format = threedsmsc::AudioFormat::M4a;
  threedsmsc::AacDecoder m4a_decoder;
  assert(m4a_decoder.open(m4a));
  // The player refuses to seek files whose decoder cannot (a raw .aac stream has no index).
  assert(m4a_decoder.can_seek());
  assert(!aac_decoder.can_seek());
  assert(mp3_decoder.can_seek());
  assert(decode_to_end(m4a_decoder, frames) == threedsmsc::DecodeResult::EndOfStream);
  assert(frames > 1);
}

void test_mp3_decoder() {
  threedsmsc::Track track;
  track.path = "tests/fixtures/silence.mp3";
  track.format = threedsmsc::AudioFormat::Mp3;
  threedsmsc::Mp3Decoder decoder;
  assert(decoder.open(track));
  assert(decoder.seek(1000));
  std::array<std::int16_t, 2304> output{};
  std::size_t written = 0;
  threedsmsc::PcmBlockInfo info;
  const threedsmsc::DecodeResult result =
      decoder.decode(output.data(), output.size(), written, info);
  assert(result == threedsmsc::DecodeResult::FrameReady);
  assert(written > 0);
  assert(info.sample_rate == 44100);
  assert(info.channels == 2);

  while (decoder.decode(output.data(), output.size(), written, info) ==
         threedsmsc::DecodeResult::FrameReady) {
  }
}

void test_controller() {
  FakeFileSystem filesystem;
  filesystem.directories["/music"] = {entry("song.mp3", false)};
  filesystem.directories["/selected"] = {entry("other.flac", false)};
  threedsmsc::Settings settings = threedsmsc::default_settings();
  settings.music_root = "/music";
  threedsmsc::LibraryController controller(filesystem, settings);
  assert(controller.scan());
  assert(controller.library().tracks.size() == 1);
  assert(controller.current_track() != nullptr);
  assert(controller.current_track()->title == "song");
  assert(controller.status() == "Library ready: 1 tracks");

  controller.set_selected_root("/selected");
  assert(controller.scan());
  assert(controller.current_track() != nullptr);
  assert(controller.current_track()->title == "other");
}

void test_multiple_scan_roots() {
  FakeFileSystem filesystem;
  filesystem.directories["/a"] = {entry("one.mp3", false), entry("nested", true)};
  filesystem.directories["/a/nested"] = {entry("two.flac", false)};
  filesystem.directories["/b"] = {entry("three.m4a", false)};
  threedsmsc::Settings settings = threedsmsc::default_settings();
  settings.music_root = "/a";
  threedsmsc::LibraryController controller(filesystem, settings);

  controller.set_selected_roots({"/a", "/b", "/a/nested"});  // /a/nested overlaps /a
  assert(controller.scan());
  assert(controller.library().tracks.size() == 3);
  assert(controller.status() == "Library ready: 3 tracks");

  // One missing folder is reported but does not discard the others.
  controller.set_selected_roots({"/b", "/gone"});
  assert(controller.scan());
  assert(controller.library().tracks.size() == 1);
  assert(controller.status().find("/gone") != std::string::npos);

  // Only missing folders: the scan fails and the previous library stays usable.
  controller.set_selected_roots({"/gone"});
  assert(!controller.scan());
  assert(controller.library().tracks.size() == 1);

  // An empty selection falls back to the default folder.
  controller.set_selected_roots({});
  assert(controller.scan());
  assert(controller.library().tracks.size() == 2);
}

void test_folder_list_and_device_root() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-folders.txt";
  assert(threedsmsc::load_folder_list(path.string()).empty());
  assert(threedsmsc::save_folder_list(path.string(), {"sdmc:/Music", "sdmc:/Downloads/albums"}));
  const std::vector<std::string> loaded = threedsmsc::load_folder_list(path.string());
  assert(loaded.size() == 2 && loaded[1] == "sdmc:/Downloads/albums");
  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);

  // Browsing starts at the device root and can walk down and back up to it.
  FakeFileSystem filesystem;
  filesystem.directories["sdmc:/"] = {entry("Music", true), entry("notes.txt", false)};
  filesystem.directories["sdmc:/Music"] = {entry("Rock", true)};
  threedsmsc::FolderBrowser browser(filesystem, "sdmc:/");
  std::string error;
  assert(browser.refresh(error));
  assert(browser.entries().size() == 1);
  assert(browser.path_of(0) == "sdmc:/Music");
  assert(browser.enter(0, error));
  assert(browser.path_of(0) == "sdmc:/Music/Rock");
  assert(browser.leave(error));
  assert(browser.at_root());
  assert(!browser.leave(error));

  // A folder that cannot be listed must not become the current folder.
  filesystem.directories["sdmc:/Music"] = {entry("Locked", true)};
  assert(browser.enter(0, error));
  assert(!browser.enter(0, error));  // sdmc:/Music/Locked has no listing
  assert(browser.current_path() == "sdmc:/Music");
  assert(browser.path_of(0) == "sdmc:/Music/Locked");
}

void test_seek_step() {
  assert(threedsmsc::next_seek_seconds(5) == 10);
  assert(threedsmsc::next_seek_seconds(10) == 15);
  assert(threedsmsc::next_seek_seconds(15) == 25);
  assert(threedsmsc::next_seek_seconds(25) == 5);
  assert(threedsmsc::next_seek_seconds(7) == 5);  // an unknown value restarts the cycle
  int seconds = 10;
  assert(threedsmsc::parse_seek_seconds("15", seconds) && seconds == 15);
  assert(!threedsmsc::parse_seek_seconds("12", seconds) && seconds == 15);  // unchanged
  assert(!threedsmsc::parse_seek_seconds("fast", seconds));
}

void test_queue() {
  threedsmsc::PlaybackQueue queue;
  assert(queue.empty());
  queue.set_tracks({make_track("a.mp3", "A", "Artist"), make_track("b.flac", "B", "Artist")});
  assert(queue.size() == 2);
  assert(queue.current() != nullptr && queue.current()->title == "A");
  assert(queue.select(1));
  assert(queue.current() != nullptr && queue.current()->title == "B");
  assert(!queue.select(2));
  assert(queue.select(0));
  const threedsmsc::Track* second = queue.next();
  assert(second != nullptr && second->title == "B");
  const threedsmsc::Track* wrapped = queue.next();
  assert(wrapped != nullptr && wrapped->title == "A");
  const threedsmsc::Track* first = queue.previous();
  assert(first != nullptr && first->title == "B");
  queue.clear();
  assert(queue.empty());
}

void test_folder_browser() {
  FakeFileSystem filesystem;
  filesystem.directories["/music"] = {entry("Album", true), entry("notes.txt", false)};
  filesystem.directories["/music/Album"] = {entry("song.mp3", false)};
  threedsmsc::FolderBrowser browser(filesystem, "/music");
  std::string error;
  assert(browser.refresh(error));
  assert(browser.entries().size() == 1);
  assert(browser.selected() == 0);
  assert(browser.enter(0, error));
  assert(browser.current_path() == "/music/Album");
  assert(browser.leave(error));
  assert(browser.current_path() == "/music");
  assert(!browser.leave(error));
  assert(!error.empty());
}

void test_queue_file() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-queue.txt";
  threedsmsc::PlaybackQueue queue;
  queue.set_tracks({make_track("a.mp3", "A", "Artist"), make_track("b.flac", "B", "Artist")});
  assert(queue.next() != nullptr);
  assert(threedsmsc::save_queue_file(path.string(), queue));

  threedsmsc::PlaybackQueue loaded;
  std::string error;
  assert(threedsmsc::load_queue_file(path.string(), loaded, &error));
  assert(error.empty());
  assert(loaded.size() == 2);
  assert(loaded.current_index() == 1);
  assert(loaded.current() != nullptr && loaded.current()->path == "b.flac");
  assert(loaded.tracks()[0].path == "a.mp3");
  assert(loaded.tracks()[1].format == threedsmsc::AudioFormat::Flac);

  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
}

}

int main() {
  test_defaults();
  test_settings_loading();
  test_invalid_settings_fall_back();
  test_settings_file();
  test_seek_step();
  test_formats_and_search();
  test_scanner();
  test_scanner_empty_and_cancelled();
  test_local_filesystem();
  test_artwork_priority();
  test_embedded_metadata();
  test_id3_numbers_and_encodings();
  test_big_cover_is_skipped();
  test_mp3_duration();
  test_browse_index();
  test_browse_navigator();
  test_aac_decoder();
  test_flac_decoder();
  test_mp3_decoder();
  test_decoders_play_to_the_end();
  test_controller();
  test_multiple_scan_roots();
  test_folder_list_and_device_root();
  test_folder_browser();
  test_queue();
  test_queue_file();
  return 0;
}
