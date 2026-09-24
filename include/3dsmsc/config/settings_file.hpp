#pragma once

#include <string>

#include "3dsmsc/config/settings.hpp"

namespace threedsmsc {

bool load_settings_file(const std::string& path, Settings& settings, std::string* error = nullptr);
bool save_settings_file(const std::string& path, const Settings& settings,
                        std::string* error = nullptr);

}
