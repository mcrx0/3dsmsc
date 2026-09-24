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
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/folder_browser.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/library/scanner.hpp"
#include "3dsmsc/library/track.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/queue/queue_file.hpp"

namespace {

class FakeFileSystem final : public threedsmsc::FileSystem {
 public:
  bool list_directory(const std::string& path, std::vector<threedsmsc::DirectoryEntry>& entries,
                      std::string& error) override {
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
  assert(settings.music_root == "sdmc:/3dmms/music/");
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
  assert(settings.music_root == "sdmc:/3dmms/music/");
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
  settings.search_case_sensitive = true;
  settings.next_button = "X2";
  assert(threedsmsc::save_settings_file(path.string(), settings, &error));
  assert(threedsmsc::load_settings_file(path.string(), settings, &error));
  assert(settings.volume == 0.35);
  assert(!settings.animation_enabled);
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
  test_formats_and_search();
  test_scanner();
  test_scanner_empty_and_cancelled();
  test_local_filesystem();
  test_artwork_priority();
  test_embedded_metadata();
  test_aac_decoder();
  test_flac_decoder();
  test_mp3_decoder();
  test_controller();
  test_folder_browser();
  test_queue();
  test_queue_file();
  return 0;
}
