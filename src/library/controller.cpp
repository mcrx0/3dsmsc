#include <string>
#include <utility>

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

bool LibraryController::scan() {
  const std::string root = settings_.scan_scope == ScanScope::Default
                               ? settings_.music_root
                               : settings_.last_selected_root;
  return scan_root(root);
}

bool LibraryController::scan_root(const std::string& root) {
  if (root.empty()) {
    library_.state = ScanState::Failed;
    library_.message = "No music folder selected";
    status_ = library_.message;
    return false;
  }
  std::string error;
  if (!scanner_.scan(root, library_, {}, &error)) {
    status_ = error;
    return false;
  }
  status_ = library_.tracks.empty()
                ? library_.message
                : "Library ready: " + std::to_string(library_.tracks.size()) + " tracks";
  return true;
}

void LibraryController::set_selected_root(const std::string& root) {
  settings_.last_selected_root = root;
  settings_.scan_scope = ScanScope::Selected;
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
