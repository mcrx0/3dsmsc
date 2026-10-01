#pragma once

#include <cstdio>
#include <memory>
#include <string>

namespace threedsmsc {

struct FileCloser {
  void operator()(std::FILE* file) const noexcept {
    if (file != nullptr)
      (void)std::fclose(file);  // nothing useful can be done if closing a read handle fails
  }
};

// An open file that closes itself, so an early return can never leak a handle.
using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

// An empty UniqueFile (test with `if (!file)`) when the file cannot be opened.
inline UniqueFile open_file(const std::string& path, const char* mode) {
  return UniqueFile(std::fopen(path.c_str(), mode));
}

}
