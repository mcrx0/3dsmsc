#pragma once

#include <string>
#include <vector>

namespace threedsmsc {

// Persists the folders chosen for scanning, one path per line.
bool save_folder_list(const std::string& path, const std::vector<std::string>& folders);
// A missing file is not an error: it yields an empty list (scan the default folder).
std::vector<std::string> load_folder_list(const std::string& path);

}
