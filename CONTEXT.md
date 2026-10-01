# 3DSMSC domain

## Track

A playable audio file identified by its filesystem path. A track may contain title, artist, album, duration, and artwork metadata.

## Album

A metadata-based grouping of tracks by artist and album name. Its tracks play in disc number, then track number order; untagged numbers sort last. Folder structure is used only as a fallback when metadata is absent.

## Artist

A metadata-based grouping of tracks. The artist is not a filesystem entity.

## Library

The result of an explicit scan of the user's selected music folders, or of the default music folder when none are selected. A Track found through overlapping folders appears once. Scanning does not run automatically.

## Music folder

A folder anywhere on the SD card that the user ticked to be included in the next scan. The selection is remembered until changed.

## Queue

An ordered temporary list of tracks used for playback. Saved playlists are a separate future concept.

## Metadata

Descriptive track information supplied by a tag reader or a safe filename fallback. Metadata never changes a track's filesystem identity.

## Artwork

An optional image associated with a track or album. Lookup prefers embedded artwork, then standardized sidecar images in the containing folder.

## Cassette view

The visual representation of playback state on the top screen. It does not own audio state or decode audio.
