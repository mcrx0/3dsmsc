#pragma once

#include <string>
#include <vector>

namespace threedsmsc {

struct DirectoryEntry {
  std::string name;
  bool is_directory;
};

class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual bool list_directory(const std::string& path, std::vector<DirectoryEntry>& entries,
                              std::string& error) = 0;
};

class LocalFileSystem final : public FileSystem {
 public:
  bool list_directory(const std::string& path, std::vector<DirectoryEntry>& entries,
                      std::string& error) override;
};

}
