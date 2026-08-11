#pragma once

// Persistent track/playlist store at [saveDir]/menu-music/library.json. Paths
// are absolute UTF-8 strings; the library is independent of FMOD and UI.

#include "../model/MenuMusicTypes.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::menumusic {

class MenuMusicLibrary {
public:
    static MenuMusicLibrary& get();

    std::filesystem::path getRootDir() const;
    std::filesystem::path getTracksDir() const;
    std::filesystem::path getCoversDir() const;
    std::filesystem::path getLibraryFile() const;

void load();                     // Idempotent.
    void save();
    bool isLoaded() const { return m_loaded; }

    std::vector<MusicTrack>& tracks() { return m_tracks; }
    const std::vector<MusicTrack>& tracks() const { return m_tracks; }
    MusicTrack* findTrack(const std::string& id);
    const MusicTrack* findTrack(const std::string& id) const;
    MusicTrack* findTrackByAudioPath(const std::string& path);
    const MusicTrack* findTrackByAudioPath(const std::string& path) const;
    void addTrack(const MusicTrack& track);
    void updateTrack(const MusicTrack& track);
    bool deleteLocalAudio(const std::string& id);
// removeTrack optionally deletes downloaded audio/cover files; local tracks stay.
    void removeTrack(const std::string& id, bool deleteFiles);
    void setFavorite(const std::string& id, bool favorite);
    void setBlacklisted(const std::string& id, bool blacklisted);
    bool hasTracks() const { return !m_tracks.empty(); }

    std::size_t importFolder(const std::filesystem::path& folder, bool recursive = true);
// force=false skips the scan while GD's downloaded-song count is unchanged.
    std::size_t syncDownloadedSongs(bool force = true);

    std::vector<MusicPlaylist>& playlists() { return m_playlists; }
    const std::vector<MusicPlaylist>& playlists() const { return m_playlists; }
    MusicPlaylist* findPlaylist(const std::string& id);
    void addPlaylist(const MusicPlaylist& pl);
    void renamePlaylist(const std::string& id, const std::string& newName);
    void removePlaylist(const std::string& id);
    void addTrackToPlaylist(const std::string& playlistId, const std::string& trackId);
    void removeTrackFromPlaylist(const std::string& playlistId, const std::string& trackId);

    PlaybackMode mode() const { return m_mode; }
    void setMode(PlaybackMode mode);

    std::string activePlaylistId() const { return m_activePlaylistId; }
    void setActivePlaylistId(const std::string& id);

    const std::string& lastTrackId() const { return m_lastTrackId; }
    void setLastTrackId(const std::string& id);

    std::string generateId(const std::string& prefix = "trk");
    static bool isAudioExtension(const std::filesystem::path& p);
    static bool isImageExtension(const std::filesystem::path& p);

// Notify listeners after persisted changes; safe to re-enter.
    using Listener = std::function<void()>;
    std::size_t addListener(Listener cb);
    void removeListener(std::size_t token);

private:
    MenuMusicLibrary();
    ~MenuMusicLibrary() = default;
    MenuMusicLibrary(const MenuMusicLibrary&) = delete;
    MenuMusicLibrary& operator=(const MenuMusicLibrary&) = delete;

    void ensureDirs();
    void notifyChanged();
    void rebuildTrackIndex();
    void markDirty();

    std::vector<MusicTrack> m_tracks;
    std::unordered_map<std::string, size_t> m_trackIndex;
    std::vector<MusicPlaylist> m_playlists;
    PlaybackMode m_mode = PlaybackMode::Disabled;
    std::string m_activePlaylistId;
    std::string m_lastTrackId;
    std::uint64_t m_idCounter = 0;
    // GD downloaded-song count at the last sync; SIZE_MAX means never synced.
    std::size_t m_syncedSongCount = static_cast<std::size_t>(-1);
    bool m_loaded = false;
    bool m_savePending = false;

    std::vector<std::pair<std::size_t, Listener>> m_listeners;
    std::size_t m_nextListenerToken = 1;
};

}
