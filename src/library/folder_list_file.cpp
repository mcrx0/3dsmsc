#include <fstream>
#include <string>
#include <vector>

#include "3dsmsc/library/folder_list_file.hpp"

namespace threedsmsc {

bool save_folder_list(const std::string& path, const std::vector<std::string>& folders) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  for (const std::string& folder : folders)
    output << folder << '\n';
  return output.good();
}

std::vector<std::string> load_folder_list(const std::string& path) {
  std::vector<std::string> folders;
  std::ifstream input(path, std::ios::binary);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty())
      folders.push_back(line);
  }
  return folders;
}

}
