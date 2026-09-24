#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/queue/queue_file.hpp"

namespace threedsmsc {
namespace {

constexpr const char* queue_header = "3dsmsc-queue-v1";

void append_track(const std::string& line, std::vector<Track>& tracks) {
  if (line.empty())
    return;
  Track track;
  track.path = line;
  track.title = line;
  track.format = format_from_path(line);
  tracks.push_back(std::move(track));
}

}

bool save_queue_file(const std::string& path, const PlaybackQueue& queue) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output << queue_header << '\n' << queue.current_index() << '\n';
  for (const Track& track : queue.tracks()) {
    output << track.path << '\n';
    if (!output)
      return false;
  }
  return output.good();
}

bool load_queue_file(const std::string& path, PlaybackQueue& queue, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error != nullptr)
      *error = "Queue file not found";
    return false;
  }
  std::string line;
  if (!std::getline(input, line)) {
    if (error != nullptr)
      *error = "Queue file is empty";
    return false;
  }
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  std::vector<Track> tracks;
  unsigned long long saved_index = 0;
  bool has_header = line == queue_header;
  if (has_header) {
    if (!std::getline(input, line)) {
      if (error != nullptr)
        *error = "Queue file has no position";
      return false;
    }
    char* end = nullptr;
    saved_index = std::strtoull(line.c_str(), &end, 10);
    if (end == line.c_str() || *end != '\0') {
      if (error != nullptr)
        *error = "Queue file position is invalid";
      return false;
    }
  } else {
    append_track(line, tracks);
  }
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    append_track(line, tracks);
  }
  if (!input.eof() && input.fail()) {
    if (error != nullptr)
      *error = "Queue file could not be read";
    return false;
  }
  queue.set_tracks(std::move(tracks));
  if (saved_index < queue.size())
    queue.select(static_cast<std::size_t>(saved_index));
  if (error != nullptr)
    error->clear();
  return true;
}

}
