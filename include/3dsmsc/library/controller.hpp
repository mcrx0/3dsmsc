#pragma once

#include <string>

#include "3dsmsc/config/settings.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/metadata.hpp"
#include "3dsmsc/library/scanner.hpp"

namespace threedsmsc {

class LibraryController {
 public:
  explicit LibraryController(FileSystem& filesystem, Settings settings);

  bool scan();
  bool scan_root(const std::string& root);
  void set_selected_root(const std::string& root);
  const LibraryIndex& library() const;
  const Track* current_track() const;
  const std::string& status() const;

 private:
  FileSystem& filesystem_;
  Settings settings_;
  LibraryIndex library_;
  std::string status_;
  EmbeddedMetadataReader embedded_metadata_reader_;
  FilenameMetadataReader filename_metadata_reader_;
  CompositeMetadataReader metadata_reader_;
  ArtworkLocator artwork_locator_;
  LibraryScanner scanner_;
};

}
