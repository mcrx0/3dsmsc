#include <cstdio>
#include <cstdlib>
#include <string>
#include "3dsmsc/library/library_cache.hpp"
int main(int argc, char** argv) {
  const int count = std::atoi(argv[2]);
  threedsmsc::LibraryIndex lib;
  lib.state = threedsmsc::ScanState::Ready;
  for (int i = 0; i < count; ++i) {
    threedsmsc::Track t{};
    t.path = "sdmc:/3dsmsc/music/Artist " + std::to_string(i % 400) + "/Album " +
             std::to_string(i % 1300) + "/Track number " + std::to_string(i) + ".mp3";
    t.title = "Song title " + std::to_string(i);
    t.artist = "Artist " + std::to_string(i % 400);
    t.album = "Album " + std::to_string(i % 1300);
    t.duration_ms = 200000 + i;
    t.track_number = i % 15;
    t.format = threedsmsc::AudioFormat::Mp3;
    lib.tracks.push_back(t);
  }
  return threedsmsc::save_library_cache(argv[1], lib) ? 0 : 1;
}
