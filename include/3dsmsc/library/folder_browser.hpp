#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "3dsmsc/library/filesystem.hpp"

namespace threedsmsc {

class FolderBrowser {
 public:
  FolderBrowser(FileSystem& filesystem, std::string root);

  bool refresh(std::string& error);
  bool enter(std::size_t index, std::string& error);
  bool leave(std::string& error);

  // Full path of the folder at `index` in entries(), or empty when out of range.
  std::string path_of(std::size_t index) const;
  const std::string& current_path() const;
  const std::vector<DirectoryEntry>& entries() const;
  bool at_root() const;

 private:
  FileSystem& filesystem_;
  std::string root_;
  std::string current_path_;
  std::vector<DirectoryEntry> entries_;
};

}
