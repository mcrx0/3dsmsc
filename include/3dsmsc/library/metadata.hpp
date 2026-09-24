#pragma once

#include <cstdint>
#include <string>

namespace threedsmsc {

struct TrackMetadata {
  std::string title;
  std::string artist;
  std::string album;
  std::uint64_t duration_ms;
};

class MetadataReader {
 public:
  virtual ~MetadataReader() = default;
  virtual bool read(const std::string& path, TrackMetadata& metadata) = 0;
};

class FilenameMetadataReader final : public MetadataReader {
 public:
  bool read(const std::string& path, TrackMetadata& metadata) override;
};

class EmbeddedMetadataReader final : public MetadataReader {
 public:
  bool read(const std::string& path, TrackMetadata& metadata) override;
};

class CompositeMetadataReader final : public MetadataReader {
 public:
  CompositeMetadataReader(MetadataReader& primary, MetadataReader& fallback)
      : primary_(primary), fallback_(fallback) {}

  bool read(const std::string& path, TrackMetadata& metadata) override;

 private:
  MetadataReader& primary_;
  MetadataReader& fallback_;
};

}
