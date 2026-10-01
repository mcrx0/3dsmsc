#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "3dsmsc/library/artwork.hpp"

namespace threedsmsc {
namespace {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string parent_path(const std::string& path) {
  const std::size_t separator = path.find_last_of("/\\:");
  return separator == std::string::npos ? std::string{} : path.substr(0, separator);
}

std::string join_path(const std::string& parent, const std::string& name) {
  if (parent.empty())
    return name;
  if (parent.back() == '/' || parent.back() == '\\')
    return parent + name;
  return parent + "/" + name;
}

int artwork_priority(const std::string& name) {
  if (name == "cover.png")
    return 0;
  if (name == "cover.jpg" || name == "cover.jpeg")
    return 1;
  if (name == "folder.png")
    return 2;
  if (name == "folder.jpg" || name == "folder.jpeg")
    return 3;
  return 4;
}

}

ArtworkLocator::ArtworkLocator(FileSystem& filesystem) : filesystem_(filesystem) {}

bool ArtworkLocator::find(const std::string& track_path, std::string& artwork_path) {
  artwork_path.clear();
  const std::string parent = parent_path(track_path);
  if (parent.empty())
    return false;
  if (has_cache_ && parent == cached_folder_) {
    artwork_path = cached_artwork_;
    return !artwork_path.empty();
  }
  std::vector<DirectoryEntry> entries;
  std::string error;
  if (!filesystem_.list_directory(parent, entries, error))
    return false;
  has_cache_ = true;
  cached_folder_ = parent;
  int best_priority = 4;
  for (const DirectoryEntry& entry : entries) {
    if (entry.is_directory)
      continue;
    const std::string name = lowercase(entry.name);
    const int priority = artwork_priority(name);
    if (priority < best_priority) {
      best_priority = priority;
      artwork_path = join_path(parent, entry.name);
    }
  }
  cached_artwork_ = artwork_path;
  return !artwork_path.empty();
}

}
