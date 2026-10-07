#pragma once

// menumusic data model: tracks, playlists, sources, and playback state.

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace paimon::menumusic {

// track source controls display and deletion behavior.
enum class TrackSource : std::uint8_t {
    Unknown = 0,
    Local,          // user-imported; not managed.
    Downloaded,     // yt-dlp; can re-download/delete.
    Vanilla,        // gd built-in; not listed.
    GeometryDash,   // newgrounds/music library download.
};

// library track; paths are absolute and durationms=0 means unknown.
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

// playlist of track ids.
struct MusicPlaylist {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds;
    std::int64_t createdUnixMs = 0;
};

// library/playlist/queue override gd's menu loop; disabled leaves it untouched.
enum class PlaybackMode : std::uint8_t {
    Disabled = 0,
    Library,
    Playlist,
    Queue,
};

// state reported by menumusicplayer.
struct PlaybackState {
    std::string currentTrackId;
    std::string currentAudioPath;
    PlaybackMode mode = PlaybackMode::Disabled;
    bool isPlaying = false;
};

}
