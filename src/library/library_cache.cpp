#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "3dsmsc/library/library_cache.hpp"
#include "3dsmsc/util/unique_file.hpp"

namespace threedsmsc {
namespace {

constexpr char magic[8] = {'3', 'D', 'S', 'M', 'S', 'C', 'L', 'B'};
constexpr std::uint32_t version = 1;
constexpr std::uint32_t max_tracks = 200000;
constexpr long max_file_bytes = 64L * 1024L * 1024L;

void put_u8(std::string& out, std::uint8_t value) {
  out.push_back(static_cast<char>(value));
}

void put_u16(std::string& out, std::uint16_t value) {
  put_u8(out, static_cast<std::uint8_t>(value & 0xFF));
  put_u8(out, static_cast<std::uint8_t>(value >> 8));
}

void put_u32(std::string& out, std::uint32_t value) {
  put_u16(out, static_cast<std::uint16_t>(value & 0xFFFF));
  put_u16(out, static_cast<std::uint16_t>(value >> 16));
}

// Strings are capped at 65535 bytes, far beyond any path or tag the player handles.
void put_string(std::string& out, const std::string& value) {
  const std::size_t length = value.size() > 0xFFFF ? 0xFFFF : value.size();
  put_u16(out, static_cast<std::uint16_t>(length));
  out.append(value, 0, length);
}

class Reader {
 public:
  Reader(const std::string& data) : data_(data) {}

  bool read_u8(std::uint8_t& value) {
    if (position_ + 1 > data_.size())
      return false;
    value = static_cast<std::uint8_t>(data_[position_++]);
    return true;
  }

  bool read_u16(std::uint16_t& value) {
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    if (!read_u8(low) || !read_u8(high))
      return false;
    value = static_cast<std::uint16_t>(low | (high << 8));
    return true;
  }

  bool read_u32(std::uint32_t& value) {
    std::uint16_t low = 0;
    std::uint16_t high = 0;
    if (!read_u16(low) || !read_u16(high))
      return false;
    value = static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 16);
    return true;
  }

  bool read_string(std::string& value) {
    std::uint16_t length = 0;
    if (!read_u16(length) || position_ + length > data_.size())
      return false;  // a length running past the end means a truncated or corrupt file
    value.assign(data_, position_, length);
    position_ += length;
    return true;
  }

  std::size_t remaining() const { return data_.size() - position_; }
  bool at_end() const { return position_ == data_.size(); }
  bool read_bytes(char* out, std::size_t count) {
    if (position_ + count > data_.size())
      return false;
    std::memcpy(out, data_.data() + position_, count);
    position_ += count;
    return true;
  }

 private:
  const std::string& data_;
  std::size_t position_ = 0;
};

std::string serialize(const LibraryIndex& library) {
  std::string data;
  data.reserve(library.tracks.size() * 160 + 32);
  data.append(magic, sizeof(magic));
  put_u32(data, version);
  put_u32(data, static_cast<std::uint32_t>(library.tracks.size()));
  for (const Track& track : library.tracks) {
    put_string(data, track.path);
    put_string(data, track.title);
    put_string(data, track.artist);
    put_string(data, track.album);
    put_string(data, track.artwork_path);
    put_u32(data, static_cast<std::uint32_t>(
                      track.duration_ms > 0xFFFFFFFFull ? 0xFFFFFFFFull : track.duration_ms));
    put_u16(data, track.track_number);
    put_u16(data, track.disc_number);
    put_u8(data, static_cast<std::uint8_t>(track.format));
  }
  return data;
}

// Writes beside the target and renames, so a failed or interrupted save never replaces a good
// cache with a partial one.
bool write_atomically(const std::string& path, const std::string& data) {
  const std::string temporary = path + ".tmp";
  UniqueFile file = open_file(temporary, "wb");
  if (file == nullptr)
    return false;
  const bool written = std::fwrite(data.data(), 1, data.size(), file.get()) == data.size();
  // Closing is what flushes to the card, so only a successful close proves the save worked.
  const bool closed = std::fclose(file.release()) == 0;
  if (!written || !closed) {
    std::remove(temporary.c_str());
    return false;
  }
  std::remove(path.c_str());  // rename() will not overwrite on every platform
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

// One large read is much faster than many small ones on an SD card. The buffer is on the heap:
// this runs on the main thread, whose whole stack is only 32 KB.
bool read_whole_file(const std::string& path, std::string& data) {
  const UniqueFile file = open_file(path, "rb");
  if (file == nullptr)
    return false;
  std::fseek(file.get(), 0, SEEK_END);
  const long file_size = std::ftell(file.get());
  std::fseek(file.get(), 0, SEEK_SET);
  if (file_size <= 0 || file_size > max_file_bytes)
    return false;
  data.assign(static_cast<std::size_t>(file_size), '\0');
  return std::fread(data.data(), 1, data.size(), file.get()) == data.size();
}

bool read_track(Reader& reader, Track& track) {
  std::uint32_t duration = 0;
  std::uint8_t format = 0;
  if (!reader.read_string(track.path) || !reader.read_string(track.title) ||
      !reader.read_string(track.artist) || !reader.read_string(track.album) ||
      !reader.read_string(track.artwork_path) || !reader.read_u32(duration) ||
      !reader.read_u16(track.track_number) || !reader.read_u16(track.disc_number) ||
      !reader.read_u8(format) || format > static_cast<std::uint8_t>(AudioFormat::Flac))
    return false;
  track.duration_ms = duration;
  track.format = static_cast<AudioFormat>(format);
  return true;
}

// Validates the header and reads every track; false for anything this version did not write.
bool parse(const std::string& data, LibraryIndex& library) {
  Reader reader(data);
  char header[sizeof(magic)] = {};
  std::uint32_t file_version = 0;
  std::uint32_t count = 0;
  if (!reader.read_bytes(header, sizeof(header)) ||
      std::memcmp(header, magic, sizeof(magic)) != 0 || !reader.read_u32(file_version) ||
      file_version != version || !reader.read_u32(count) || count > max_tracks)
    return false;
  // A track record is at least 19 bytes (five empty strings, duration, two numbers, format), so
  // the file itself bounds the count. Without this a corrupt count would make reserve() below
  // ask for tens of megabytes, which the 3DS cannot spare.
  constexpr std::size_t smallest_record = 5 * 2 + 4 + 2 + 2 + 1;
  if (count > reader.remaining() / smallest_record)
    return false;
  library.tracks.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    Track track;
    if (!read_track(reader, track))
      return false;
    library.tracks.push_back(std::move(track));
  }
  return reader.at_end();  // trailing bytes: not a file this version wrote
}

}

bool save_library_cache(const std::string& path, const LibraryIndex& library) {
  return write_atomically(path, serialize(library));
}

bool load_library_cache(const std::string& path, LibraryIndex& library) {
  library = LibraryIndex{};
  std::string data;
  LibraryIndex loaded;
  if (!read_whole_file(path, data) || !parse(data, loaded))
    return false;
  loaded.state = ScanState::Ready;
  library = std::move(loaded);
  return true;
}

}
