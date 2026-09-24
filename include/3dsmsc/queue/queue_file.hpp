#pragma once

#include <string>

#include "3dsmsc/queue/playback_queue.hpp"

namespace threedsmsc {

bool save_queue_file(const std::string& path, const PlaybackQueue& queue);
bool load_queue_file(const std::string& path, PlaybackQueue& queue, std::string* error);

}
