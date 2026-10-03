#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace threedsmsc {

// Extracts the cover picture stored inside an audio file: an ID3v2 APIC frame (MP3, also at the
// start of some FLAC files), a FLAC PICTURE block, or an MP4/M4A `covr` atom. The front cover is
// preferred when a file holds several pictures. `image` receives the PNG or JPEG bytes unchanged.
//
// Returns false when the file has no picture, is damaged, or the picture is larger than
// `max_bytes`; nothing is read beyond the tag, and nothing is thrown.
bool read_embedded_cover(const std::string& audio_path, std::size_t max_bytes,
                         std::vector<unsigned char>& image);

}
