#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "3dsmsc/library/filesystem.hpp"

namespace threedsmsc {
namespace {

std::string join_path(const std::string& parent, const std::string& name) {
  if (parent.empty())
    return name;
  if (parent.back() == '/' || parent.back() == '\\')
    return parent + name;
  return parent + "/" + name;
}

}

bool LocalFileSystem::list_directory(const std::string& path, std::vector<DirectoryEntry>& entries,
                                     std::string& error) {
  entries.clear();
  DIR* directory = opendir(path.c_str());
  if (directory == nullptr) {
    error = std::strerror(errno);
    return false;
  }
  while (dirent* item = readdir(directory)) {
    const std::string name(item->d_name);
    if (name == "." || name == "..") {
      continue;
    }
    struct stat file_status{};
    const std::string full_path = join_path(path, name);
    if (lstat(full_path.c_str(), &file_status) != 0) {
      continue;
    }
    if (S_ISLNK(file_status.st_mode)) {
      continue;
    }
    DirectoryEntry entry;
    entry.name = name;
    entry.is_directory = S_ISDIR(file_status.st_mode);
    entries.push_back(std::move(entry));
  }
  closedir(directory);
  std::sort(entries.begin(), entries.end(),
            [](const DirectoryEntry& left, const DirectoryEntry& right) {
              return left.name < right.name;
            });
  error.clear();
  return true;
}

}
