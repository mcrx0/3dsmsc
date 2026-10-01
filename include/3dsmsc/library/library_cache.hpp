#pragma once

#include <string>

#include "3dsmsc/library/library_index.hpp"

namespace threedsmsc {

// The scanned library is saved after each successful scan and loaded at start-up, so the
// Browse and Search screens work without scanning again. A scan is still only ever started by
// the user; the cache simply remembers its result.
//
// Binary layout (little-endian): magic "3DSMSCLB", u32 version, u32 track count, then per
// track the strings path/title/artist/album/artwork (u16 length + bytes), duration_ms (u32),
// track number (u16), disc number (u16) and audio format (u8).
bool save_library_cache(const std::string& path, const LibraryIndex& library);

// Fails (leaving `library` empty) when the file is missing, from another version, truncated,
// or corrupt, so a damaged cache can never produce a half-filled library.
bool load_library_cache(const std::string& path, LibraryIndex& library);

}
