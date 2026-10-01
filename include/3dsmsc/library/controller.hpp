#pragma once

#include <string>
#include <vector>

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/library/scanner.hpp"

namespace threedsmsc {

class LibraryController {
 public:
  explicit LibraryController(FileSystem& filesystem, Settings settings);

  bool scan(const ScanCallbacks& callbacks = {});
  bool scan_root(const std::string& root, const ScanCallbacks& callbacks = {});
  bool scan_roots(const std::vector<std::string>& roots, const ScanCallbacks& callbacks = {});
  // Uses a library loaded from the cache instead of scanning. Ignored when it has no tracks.
  bool adopt_cached(LibraryIndex index);
  void set_selected_root(const std::string& root);
  // Folders to scan; an empty list scans the configured default folder.
  void set_selected_roots(std::vector<std::string> roots);
  const LibraryIndex& library() const;
  const Track* current_track() const;
  const std::string& status() const;

 private:
  FileSystem& filesystem_;
  Settings settings_;
  std::vector<std::string> selected_roots_;
  LibraryIndex library_;
  std::string status_;
  EmbeddedMetadataReader embedded_metadata_reader_;
  FilenameMetadataReader filename_metadata_reader_;
  CompositeMetadataReader metadata_reader_;
  ArtworkLocator artwork_locator_;
  LibraryScanner scanner_;
};

}
