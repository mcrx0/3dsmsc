#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "3dsmsc/library/artwork.hpp"
#include "3dsmsc/library/filesystem.hpp"
#include "3dsmsc/library/library_index.hpp"
#include "3dsmsc/library/metadata.hpp"

namespace threedsmsc {

struct ScanOptions {
  bool exclude_hidden;
  std::uint32_t max_tracks;
};

using TrackVisitor = void (*)(void* context, const Track& track);
using CancelPredicate = bool (*)(void* context);

struct ScanCallbacks {
  void* context;
  TrackVisitor on_track;
  CancelPredicate is_cancelled;
};

class LibraryScanner {
 public:
  explicit LibraryScanner(FileSystem& filesystem, ScanOptions options = {},
                          MetadataReader* metadata_reader = nullptr,
                          ArtworkLocator* artwork_locator = nullptr);
  bool scan(const std::string& root, LibraryIndex& result, const ScanCallbacks& callbacks = {},
            std::string* error = nullptr);

 private:
  bool scan_directory(const std::string& path, LibraryIndex& result, const ScanCallbacks& callbacks,
                      std::string& error);

  FileSystem& filesystem_;
  ScanOptions options_;
  MetadataReader* metadata_reader_;
  ArtworkLocator* artwork_locator_;
};

}
