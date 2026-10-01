#pragma once

#include <string>

#include "3dsmsc/library/filesystem.hpp"

namespace threedsmsc {

class ArtworkLocator {
 public:
  explicit ArtworkLocator(FileSystem& filesystem);

  bool find(const std::string& track_path, std::string& artwork_path);

 private:
  FileSystem& filesystem_;
  // Tracks of one album are visited in a row, so remembering the last folder's answer
  // avoids listing it again for every track (slow on an SD card).
  std::string cached_folder_;
  std::string cached_artwork_;
  bool has_cache_ = false;
};

}
