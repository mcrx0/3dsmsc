#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/library/scanner.hpp"

namespace threedsmsc {
namespace {

std::string join_path(const std::string& parent, const std::string& name) {
  if (parent.empty())
    return name;
  if (parent.back() == '/' || parent.back() == '\\')
    return parent + name;
  return parent + "/" + name;
}

bool hidden_name(const std::string& name) {
  return !name.empty() && name.front() == '.';
}

}

LibraryScanner::LibraryScanner(FileSystem& filesystem, ScanOptions options,
                               MetadataReader* metadata_reader, ArtworkLocator* artwork_locator)
    : filesystem_(filesystem),
      options_(options),
      metadata_reader_(metadata_reader),
      artwork_locator_(artwork_locator) {
  if (options_.max_tracks == 0)
    options_.max_tracks = 10000;
}

bool LibraryScanner::scan(const std::string& root, LibraryIndex& result,
                          const ScanCallbacks& callbacks, std::string* error) {
  result.root = root;
  result.tracks.clear();
  result.state = ScanState::NotScanned;
  result.message.clear();
  std::string scan_error;
  if (!scan_directory(root, result, callbacks, scan_error)) {
    result.state = result.message == "Scan cancelled" ? ScanState::Cancelled : ScanState::Failed;
    if (error != nullptr)
      *error = scan_error;
    return false;
  }
  result.state = ScanState::Ready;
  if (result.tracks.empty())
    result.message = empty_library_message();
  if (error != nullptr)
    error->clear();
  return true;
}

bool LibraryScanner::scan_directory(const std::string& path, LibraryIndex& result,
                                    const ScanCallbacks& callbacks, std::string& error) {
  std::vector<DirectoryEntry> entries;
  if (!filesystem_.list_directory(path, entries, error)) {
    result.message = error;
    return false;
  }
  for (const DirectoryEntry& entry : entries) {
    if (options_.exclude_hidden && hidden_name(entry.name)) {
      continue;
    }
    if (callbacks.is_cancelled != nullptr && callbacks.is_cancelled(callbacks.context)) {
      result.message = "Scan cancelled";
      error = result.message;
      return false;
    }
    const std::string full_path = join_path(path, entry.name);
    if (entry.is_directory) {
      if (!scan_directory(full_path, result, callbacks, error)) {
        return false;
      }
      continue;
    }
    if (!is_supported_path(full_path)) {
      continue;
    }
    if (result.tracks.size() >= options_.max_tracks) {
      result.message = "Track limit reached";
      error = result.message;
      return false;
    }
    Track track{};
    track.path = full_path;
    track.format = format_from_path(full_path);
    TrackMetadata metadata{};
    if (metadata_reader_ != nullptr && metadata_reader_->read(track.path, metadata)) {
      track.title = std::move(metadata.title);
      track.artist = std::move(metadata.artist);
      track.album = std::move(metadata.album);
      track.duration_ms = metadata.duration_ms;
      track.track_number = metadata.track_number;
      track.disc_number = metadata.disc_number;
    }
    if (artwork_locator_ != nullptr) {
      artwork_locator_->find(track.path, track.artwork_path);
    }
    result.tracks.push_back(std::move(track));
    if (callbacks.on_track != nullptr) {
      callbacks.on_track(callbacks.context, result.tracks.back());
    }
  }
  return true;
}

}
