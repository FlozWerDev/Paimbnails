#pragma once

// MenuMusic data model: tracks, playlists, sources, and playback state.

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace paimon::menumusic {

// Track source controls display and deletion behavior.
enum class TrackSource : std::uint8_t {
    Unknown = 0,
    Local,          // User-imported; not managed.
    Downloaded,     // yt-dlp; can re-download/delete.
    Vanilla,        // GD built-in; not listed.
    GeometryDash,   // Newgrounds/Music Library download.
};

// Library track; paths are absolute and durationMs=0 means unknown.
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
    bool favorite = false;
    bool blacklisted = false;
};

// Playlist of track IDs.
struct MusicPlaylist {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds;
    std::int64_t createdUnixMs = 0;
};

// Library/Playlist/Queue override GD's menu loop; Disabled leaves it untouched.
enum class PlaybackMode : std::uint8_t {
    Disabled = 0,
    Library,
    Playlist,
    Queue,
};

// State reported by MenuMusicPlayer.
struct PlaybackState {
    std::string currentTrackId;
    std::string currentAudioPath;
    PlaybackMode mode = PlaybackMode::Disabled;
    bool isPlaying = false;
    std::int32_t positionMs = 0;
    std::int32_t lengthMs = 0;
};

}
