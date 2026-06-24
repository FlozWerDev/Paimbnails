#pragma once

// Central data types for the MenuMusic system.
// Separates the data model (track, playlist, source) from the services that consume it.

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace paimon::menumusic {

// Track source — determines the displayed icon and remove logic
// (downloaded tracks can be re-downloaded/deleted; imported ones cannot).
enum class TrackSource : std::uint8_t {
    Unknown = 0,
    Local,          // user-imported file; not managed
    Downloaded,     // downloaded via yt-dlp; can re-download/delete
    Vanilla,        // GD built-in track (menuLoop.mp3, etc). Not listed.
};

// A track in the library.
// audioPath: absolute path on disk (.mp3/.ogg/.wav/.flac/.m4a/.opus).
// coverPath: local image, or empty.
// sourceUrl: original link, only if downloaded.
// addedUnixMs: timestamp for date sorting.
// durationMs: 0 = unknown.
struct MusicTrack {
    std::string id;
    std::string audioPath;
    std::string coverPath;
    std::string displayName;
    std::string artist;
    std::string sourceUrl;
    TrackSource source = TrackSource::Local;
    std::int64_t addedUnixMs = 0;
    std::int32_t durationMs = 0;
};

// Playlist — thin wrapper over track IDs; doesn't duplicate track data.
struct MusicPlaylist {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds;
    std::int64_t createdUnixMs = 0;
};

// Playback mode.
// Library = shuffle over all tracks (default).
// Playlist = shuffle over the active playlist.
// Queue = play a specific track pinned from the UI.
// Disabled = no override; leave GD's vanilla menu loop.
enum class PlaybackMode : std::uint8_t {
    Disabled = 0,
    Library,
    Playlist,
    Queue,
};

// Playback state — used by MenuMusicPlayer to notify the UI.
struct PlaybackState {
    std::string currentTrackId;
    std::string currentAudioPath;
    PlaybackMode mode = PlaybackMode::Disabled;
    bool isPlaying = false;
    std::int32_t positionMs = 0;
    std::int32_t lengthMs = 0;
};

} // namespace paimon::menumusic
