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
  void select(std::size_t index);

  const std::string& current_path() const;
  const std::vector<DirectoryEntry>& entries() const;
  std::size_t selected() const;
  bool at_root() const;

 private:
  FileSystem& filesystem_;
  std::string root_;
  std::string current_path_;
  std::vector<DirectoryEntry> entries_;
  std::size_t selected_ = 0;
};

}
