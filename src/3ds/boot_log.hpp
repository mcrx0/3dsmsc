#pragma once

#include <cstdio>

// The version comes from CMake's APP_VERSION, so there is one place to change it.
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace threedsmsc {

// Appends a line to sdmc:/3dsmsc/boot.log so a hang or a stall can be located after the fact.
inline void boot_log(const char* message) {
  std::FILE* file = std::fopen("sdmc:/3dsmsc/boot.log", "a");
  if (file == nullptr)
    return;
  std::fprintf(file, "%s\n", message);
  std::fclose(file);
}

}
