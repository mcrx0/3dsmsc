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

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/library/artwork.hpp"
#include "3dsmsc/library/browse_index.hpp"
#include "3dsmsc/library/controller.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/folder_browser.hpp"
#include "3dsmsc/library/folder_list_file.hpp"
#include "3dsmsc/library/library_cache.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/library/scanner.hpp"
#include "3dsmsc/library/track.hpp"
#include "3dsmsc/queue/playback_queue.hpp"
#include "3dsmsc/ui/browse_navigator.hpp"
#include "test_suites.hpp"
#include "test_support.hpp"

namespace {

using test::entry;
using test::FakeFileSystem;
using test::make_track;

struct VisitContext {
  std::size_t count = 0;
};

void visit_track(void* context, const threedsmsc::Track&) {
  ++static_cast<VisitContext*>(context)->count;
}

bool cancel_scan(void* context) {
  return *static_cast<bool*>(context);
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
  assert(library.message == "Go to Settings > Full scan again");

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

  // With several missing folders only the first is reported (a missing brace once made every
  // later one append its message too).
  controller.set_selected_roots({"/b", "/gone-one", "/gone-two"});
  assert(controller.scan());
  assert(controller.status() == "Library ready: 1 tracks (/gone-one: missing directory)");

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

void test_folder_browser() {
  FakeFileSystem filesystem;
  filesystem.directories["/music"] = {entry("Album", true), entry("notes.txt", false)};
  filesystem.directories["/music/Album"] = {entry("song.mp3", false)};
  threedsmsc::FolderBrowser browser(filesystem, "/music");
  std::string error;
  assert(browser.refresh(error));
  assert(browser.entries().size() == 1);
  assert(browser.enter(0, error));
  assert(browser.current_path() == "/music/Album");
  assert(browser.leave(error));
  assert(browser.current_path() == "/music");
  assert(!browser.leave(error));
  assert(!error.empty());
}

void test_library_cache() {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "3dsmsc-lib.cache";
  std::error_code cleanup_error;
  std::filesystem::remove(path, cleanup_error);
  threedsmsc::LibraryIndex missing;
  assert(!threedsmsc::load_library_cache(path.string(), missing));  // no cache yet

  threedsmsc::LibraryIndex original;
  original.state = threedsmsc::ScanState::Ready;
  for (int index = 0; index < 300; ++index) {
    threedsmsc::Track track{};
    track.path = "sdmc:/Music/Caf\xC3\xA9/track " + std::to_string(index) + ".mp3";
    track.title = index % 7 == 0 ? "" : "T\xC3\xADtulo " + std::to_string(index);
    track.artist = "Bj\xC3\xB6rk";
    track.album = index % 2 == 0 ? "Album \xE2\x9C\x93" : "";
    track.artwork_path = index % 5 == 0 ? "sdmc:/Music/cover.jpg" : "";
    track.duration_ms = 1000ull * static_cast<unsigned>(index) + 7;
    track.track_number = static_cast<std::uint16_t>(index % 20);
    track.disc_number = static_cast<std::uint16_t>(index % 3);
    track.format = index % 2 == 0 ? threedsmsc::AudioFormat::Mp3 : threedsmsc::AudioFormat::Flac;
    original.tracks.push_back(track);
  }
  assert(threedsmsc::save_library_cache(path.string(), original));
  threedsmsc::LibraryIndex loaded;
  assert(threedsmsc::load_library_cache(path.string(), loaded));
  assert(loaded.state == threedsmsc::ScanState::Ready);
  assert(loaded.tracks.size() == original.tracks.size());
  for (std::size_t index = 0; index < original.tracks.size(); ++index) {
    const threedsmsc::Track& a = original.tracks[index];
    const threedsmsc::Track& b = loaded.tracks[index];
    assert(a.path == b.path && a.title == b.title && a.artist == b.artist && a.album == b.album);
    assert(a.artwork_path == b.artwork_path && a.duration_ms == b.duration_ms);
    assert(a.track_number == b.track_number && a.disc_number == b.disc_number);
    assert(a.format == b.format);
  }

  // Saving again replaces the old cache and leaves no temporary file behind.
  original.tracks.resize(5);
  assert(threedsmsc::save_library_cache(path.string(), original));
  assert(threedsmsc::load_library_cache(path.string(), loaded) && loaded.tracks.size() == 5);
  assert(!std::filesystem::exists(path.string() + ".tmp"));

  // Damaged files are rejected whole, never half-loaded.
  std::ifstream input(path, std::ios::binary);
  std::stringstream stream;
  stream << input.rdbuf();
  const std::string good = stream.str();
  input.close();
  const auto write_and_load = [&](const std::string& bytes) {
    {
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      output << bytes;
    }
    threedsmsc::LibraryIndex result;
    const bool ok = threedsmsc::load_library_cache(path.string(), result);
    assert(ok || result.tracks.empty());
    return ok;
  };
  assert(write_and_load(good));
  assert(!write_and_load(good.substr(0, good.size() / 2)));  // truncated
  assert(!write_and_load(good.substr(0, good.size() - 1)));  // one byte short
  assert(!write_and_load(good + "x"));                       // trailing junk
  assert(!write_and_load("NOTACACHE" + good.substr(9)));     // wrong magic
  std::string other_version = good;
  other_version[8] = 9;  // version field
  assert(!write_and_load(other_version));
  std::string huge_count = good;
  huge_count[12] = '\xFF';
  huge_count[13] = '\xFF';
  huge_count[14] = '\xFF';
  huge_count[15] = '\x7F';  // absurd track count
  assert(!write_and_load(huge_count));
  // A plausible-looking count that the file is far too small to hold is rejected up front.
  std::string inflated = good;
  inflated[12] = '\xFF';
  inflated[13] = '\xFF';
  inflated[14] = '\x02';
  inflated[15] = '\x00';  // 196607 tracks claimed, a handful present
  assert(!write_and_load(inflated));
  assert(!write_and_load(""));
  std::filesystem::remove(path, cleanup_error);

  // The controller adopts a cached library, but not an empty one.
  FakeFileSystem filesystem;
  threedsmsc::LibraryController controller(filesystem, threedsmsc::default_settings());
  assert(!controller.adopt_cached(threedsmsc::LibraryIndex{}));
  threedsmsc::LibraryIndex cached;
  cached.tracks.push_back(make_track("a.mp3", "A", "Artist"));
  assert(controller.adopt_cached(std::move(cached)));
  assert(controller.library().tracks.size() == 1);
  assert(controller.status() == "Library ready: 1 tracks (saved)");
}

void test_browse_index() {
  threedsmsc::LibraryIndex library{};
  const auto add = [&](const char* title, const char* artist, const char* album, std::uint16_t disc,
                       std::uint16_t number) {
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

}

void run_library_tests() {
  test_formats_and_search();
  test_scanner();
  test_scanner_empty_and_cancelled();
  test_local_filesystem();
  test_artwork_priority();
  test_controller();
  test_multiple_scan_roots();
  test_folder_list_and_device_root();
  test_folder_browser();
  test_library_cache();
  test_browse_index();
}
