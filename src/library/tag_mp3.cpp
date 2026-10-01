#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "tag_readers.hpp"

namespace threedsmsc::tags {
namespace {

struct Mp3FrameHeader {
  unsigned bitrate_kbps = 0;
  unsigned sample_rate = 0;
  unsigned samples_per_frame = 0;
  unsigned xing_offset = 0;  // from the start of the frame
  unsigned vbri_offset = 0;
};

constexpr std::size_t scan_window_bytes = 4096;

// Decodes the 4-byte MPEG audio frame header. False when the bytes are not a valid header.
bool parse_header(const unsigned char* h, Mp3FrameHeader& out) {
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
    return false;
  const unsigned version = (h[1] >> 3) & 3;  // 0: MPEG 2.5, 2: MPEG 2, 3: MPEG 1
  const unsigned layer = (h[1] >> 1) & 3;    // 1: layer III, 2: layer II, 3: layer I
  const unsigned bitrate_index = (h[2] >> 4) & 0xF;
  const unsigned rate_index = (h[2] >> 2) & 3;
  if (version == 1 || layer == 0 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3)
    return false;
  static const unsigned mpeg1[3][14] = {
      {32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},  // layer I
      {32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},     // layer II
      {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};     // layer III
  static const unsigned mpeg2[3][14] = {
      {32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
      {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
  static const unsigned rates[3][3] = {{11025, 12000, 8000}, {0, 0, 0}, {22050, 24000, 16000}};
  static const unsigned rates_mpeg1[3] = {44100, 48000, 32000};
  const bool mpeg1_stream = version == 3;
  const unsigned layer_row = layer == 3 ? 0 : (layer == 2 ? 1 : 2);
  out.bitrate_kbps = (mpeg1_stream ? mpeg1 : mpeg2)[layer_row][bitrate_index - 1];
  out.sample_rate = mpeg1_stream ? rates_mpeg1[rate_index] : rates[version][rate_index];
  out.samples_per_frame = layer == 3 ? 384 : (layer == 2 ? 1152 : (mpeg1_stream ? 1152 : 576));
  const bool mono = ((h[3] >> 6) & 3) == 3;
  out.xing_offset = mpeg1_stream ? (mono ? 21 : 36) : (mono ? 13 : 21);
  out.vbri_offset = 36;
  return out.sample_rate != 0;
}

// The frame count stored in a Xing/Info or VBRI header at the start of the first frame, or 0.
unsigned stored_frame_count(const unsigned char* buffer, std::size_t bytes, std::size_t frame,
                            const Mp3FrameHeader& header) {
  const auto tag_at = [&](unsigned offset, const char* name) {
    return frame + offset + 4 <= bytes && std::memcmp(buffer + frame + offset, name, 4) == 0;
  };
  const bool xing = tag_at(header.xing_offset, "Xing") || tag_at(header.xing_offset, "Info");
  if (xing && frame + header.xing_offset + 12 <= bytes &&
      (read_u32_be(buffer + frame + header.xing_offset + 4) & 1U) != 0)
    return read_u32_be(buffer + frame + header.xing_offset + 8);
  if (tag_at(header.vbri_offset, "VBRI") && frame + header.vbri_offset + 18 <= bytes)
    return read_u32_be(buffer + frame + header.vbri_offset + 14);
  return 0;
}

}

// A Xing/Info or VBRI header in the first frame holds the exact frame count; otherwise the length
// is estimated from the bitrate, which is exact for constant-bitrate files.
std::uint64_t mp3_duration_ms(std::FILE* file, long audio_start) {
  if (std::fseek(file, 0, SEEK_END) != 0)
    return 0;
  const long file_size = std::ftell(file);
  if (file_size <= audio_start || std::fseek(file, audio_start, SEEK_SET) != 0)
    return 0;
  unsigned char buffer[scan_window_bytes];
  const std::size_t bytes = std::fread(buffer, 1, sizeof(buffer), file);
  for (std::size_t frame = 0; frame + 4 <= bytes; ++frame) {
    Mp3FrameHeader header;
    if (!parse_header(buffer + frame, header))
      continue;
    const unsigned frames = stored_frame_count(buffer, bytes, frame, header);
    if (frames != 0) {
      return static_cast<std::uint64_t>(frames) * header.samples_per_frame * 1000ULL /
             header.sample_rate;
    }
    const auto audio_bytes = static_cast<std::uint64_t>(file_size - audio_start);
    return audio_bytes * 8ULL / header.bitrate_kbps;  // bits / (kbit/s) = milliseconds
  }
  return 0;
}

bool read_untagged_mp3(std::FILE* file, TrackMetadata& metadata) {
  metadata.duration_ms = mp3_duration_ms(file, 0);
  return metadata.duration_ms != 0;
}

}
