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

bool fail(std::string* error, const char* message) {
  if (error != nullptr)
    *error = message;
  return false;
}

// Reads one line without its line ending (files may come from a PC with CRLF endings).
bool read_line(std::istream& input, std::string& line) {
  if (!std::getline(input, line))
    return false;
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  return true;
}

bool parse_position(const std::string& line, unsigned long long& position) {
  char* end = nullptr;
  position = std::strtoull(line.c_str(), &end, 10);
  return end != line.c_str() && *end == '\0';
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
  if (!input)
    return fail(error, "Queue file not found");
  std::string line;
  if (!read_line(input, line))
    return fail(error, "Queue file is empty");
  std::vector<Track> tracks;
  unsigned long long saved_index = 0;
  if (line == queue_header) {
    if (!read_line(input, line))
      return fail(error, "Queue file has no position");
    if (!parse_position(line, saved_index))
      return fail(error, "Queue file position is invalid");
  } else {
    append_track(line, tracks);  // an older file with no header: the first line is a track
  }
  while (read_line(input, line))
    append_track(line, tracks);
  if (!input.eof() && input.fail())
    return fail(error, "Queue file could not be read");
  queue.set_tracks(std::move(tracks));
  if (saved_index < queue.size())
    queue.select(static_cast<std::size_t>(saved_index));
  if (error != nullptr)
    error->clear();
  return true;
}

}
