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
};

}
