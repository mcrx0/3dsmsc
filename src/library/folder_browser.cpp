#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/library/folder_browser.hpp"

namespace threedsmsc {
namespace {

std::string join_path(const std::string& parent, const std::string& name) {
  if (parent.empty())
    return name;
  if (parent.back() == '/' || parent.back() == '\\')
    return parent + name;
  return parent + "/" + name;
}

std::string parent_path(const std::string& path) {
  const std::size_t separator = path.find_last_of("/\\:");
  return separator == std::string::npos ? std::string{} : path.substr(0, separator);
}

}

FolderBrowser::FolderBrowser(FileSystem& filesystem, std::string root)
    : filesystem_(filesystem), root_(std::move(root)), current_path_(root_) {}

bool FolderBrowser::refresh(std::string& error) {
  std::vector<DirectoryEntry> all_entries;
  if (!filesystem_.list_directory(current_path_, all_entries, error))
    return false;
  entries_.clear();
  for (const DirectoryEntry& entry : all_entries) {
    if (entry.is_directory)
      entries_.push_back(entry);
  }
  if (selected_ >= entries_.size())
    selected_ = entries_.empty() ? 0 : entries_.size() - 1;
  error.clear();
  return true;
}

bool FolderBrowser::enter(std::size_t index, std::string& error) {
  if (index >= entries_.size()) {
    error = "Folder selection is unavailable";
    return false;
  }
  current_path_ = join_path(current_path_, entries_[index].name);
  selected_ = 0;
  return refresh(error);
}

bool FolderBrowser::leave(std::string& error) {
  if (current_path_ == root_) {
    error = "Already at the library root";
    return false;
  }
  current_path_ = parent_path(current_path_);
  selected_ = 0;
  return refresh(error);
}

void FolderBrowser::select(std::size_t index) {
  if (entries_.empty()) {
    selected_ = 0;
    return;
  }
  selected_ = std::min(index, entries_.size() - 1);
}

const std::string& FolderBrowser::current_path() const {
  return current_path_;
}

const std::vector<DirectoryEntry>& FolderBrowser::entries() const {
  return entries_;
}

std::size_t FolderBrowser::selected() const {
  return selected_;
}

bool FolderBrowser::at_root() const {
  return current_path_ == root_;
}

}
