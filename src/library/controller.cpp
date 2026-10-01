#include <set>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/library/controller.hpp"

namespace threedsmsc {
namespace {

constexpr std::uint32_t default_max_tracks = 10000;

}

LibraryController::LibraryController(FileSystem& filesystem, Settings settings)
    : filesystem_(filesystem),
      settings_(std::move(settings)),
      library_(),
      status_("No music folder scanned"),
      embedded_metadata_reader_(),
      filename_metadata_reader_(),
      metadata_reader_(embedded_metadata_reader_, filename_metadata_reader_),
      artwork_locator_(filesystem_),
      scanner_(filesystem_, {settings_.exclude_hidden, default_max_tracks}, &metadata_reader_,
               &artwork_locator_) {}

bool LibraryController::scan(const ScanCallbacks& callbacks) {
  if (!selected_roots_.empty())
    return scan_roots(selected_roots_, callbacks);
  return scan_root(settings_.scan_scope == ScanScope::Default ? settings_.music_root
                                                              : settings_.last_selected_root,
                   callbacks);
}

bool LibraryController::scan_root(const std::string& root, const ScanCallbacks& callbacks) {
  if (root.empty()) {
    library_.state = ScanState::Failed;
    library_.message = "No music folder selected";
    status_ = library_.message;
    return false;
  }
  return scan_roots({root}, callbacks);
}

bool LibraryController::scan_roots(const std::vector<std::string>& roots,
                                   const ScanCallbacks& callbacks) {
  LibraryIndex combined;
  combined.root = roots.size() == 1 ? roots.front() : std::to_string(roots.size()) + " folders";
  combined.state = ScanState::Ready;
  std::set<std::string> seen;
  std::string first_error;
  for (const std::string& root : roots) {
    LibraryIndex scanned;
    std::string error;
    if (!scanner_.scan(root, scanned, callbacks, &error)) {
      // A failed or cancelled scan keeps the previous library usable.
      if (scanned.state == ScanState::Cancelled) {
        status_ = error;
        return false;
      }
      if (first_error.empty()) {
        first_error = root;
        first_error += ": ";
        first_error += error;
      }
      continue;
    }
    // Selected folders may overlap (a folder and one of its sub-folders).
    for (Track& track : scanned.tracks) {
      if (seen.insert(track.path).second)
        combined.tracks.push_back(std::move(track));
    }
  }
  if (combined.tracks.empty() && !first_error.empty()) {
    status_ = first_error;
    return false;
  }
  if (combined.tracks.empty())
    combined.message = empty_library_message();
  library_ = std::move(combined);
  status_ = library_.tracks.empty()
                ? library_.message
                : "Library ready: " + std::to_string(library_.tracks.size()) + " tracks";
  if (!first_error.empty() && !library_.tracks.empty())
    status_ += " (" + first_error + ")";
  return true;
}

bool LibraryController::adopt_cached(LibraryIndex index) {
  if (index.tracks.empty())
    return false;
  library_ = std::move(index);
  library_.state = ScanState::Ready;
  status_ = "Library ready: " + std::to_string(library_.tracks.size()) + " tracks (saved)";
  return true;
}

void LibraryController::set_selected_root(const std::string& root) {
  settings_.last_selected_root = root;
  settings_.scan_scope = ScanScope::Selected;
  selected_roots_.clear();
}

void LibraryController::set_selected_roots(std::vector<std::string> roots) {
  selected_roots_ = std::move(roots);
}

const LibraryIndex& LibraryController::library() const {
  return library_;
}

const Track* LibraryController::current_track() const {
  return library_.tracks.empty() ? nullptr : &library_.tracks.front();
}

const std::string& LibraryController::status() const {
  return status_;
}

}
