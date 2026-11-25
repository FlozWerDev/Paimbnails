#pragma once

// persistent track/playlist store at [savedir]/menu-music/library.json. paths
// are absolute utf-8 strings; the library is independent of fmod and ui.

#include "../model/MenuMusicTypes.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::menumusic {

struct DownloadedSongSnapshot;

class MenuMusicLibrary {
public:
    static MenuMusicLibrary& get();

    std::filesystem::path getRootDir() const;
    std::filesystem::path getTracksDir() const;
    std::filesystem::path getCoversDir() const;
    std::filesystem::path getLibraryFile() const;

void load();                     // idempotent.
    void save();
    bool isLoaded() const { return m_loaded; }
    bool hasFailedLoad() const { return m_loadFailed; }

    std::vector<MusicTrack>& tracks() { return m_tracks; }
    const std::vector<MusicTrack>& tracks() const { return m_tracks; }
    MusicTrack* findTrack(const std::string& id);
    const MusicTrack* findTrack(const std::string& id) const;
    MusicTrack* findTrackByAudioPath(const std::string& path);
    const MusicTrack* findTrackByAudioPath(const std::string& path) const;
    void addTrack(const MusicTrack& track);
    void updateTrack(const MusicTrack& track);
    bool deleteLocalAudio(const std::string& id);
// removetrack optionally deletes downloaded audio/cover files; local tracks stay.
    void removeTrack(const std::string& id, bool deleteFiles);
    void setFavorite(const std::string& id, bool favorite);
    void setBlacklisted(const std::string& id, bool blacklisted);
    bool hasTracks() const { return !m_tracks.empty(); }

    std::size_t importFolder(const std::filesystem::path& folder, bool recursive = true);
// force=false skips while GD metadata is unchanged; downloads invalidate the snapshot.
    std::size_t syncDownloadedSongs(bool force = true);
    void syncDownloadedSongsAsync();
    void invalidateDownloadedSongs() {
        ++m_downloadSyncGeneration;
        m_syncedSongCount = static_cast<std::size_t>(-1);
    }
    bool isSyncingDownloadedSongs() const { return m_downloadSyncPending; }

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

// notify listeners after persisted changes; safe to re-enter.
    using Listener = std::function<void()>;
    std::size_t addListener(Listener cb);
    void removeListener(std::size_t token);

private:
    MenuMusicLibrary();
    ~MenuMusicLibrary() = default;
    MenuMusicLibrary(const MenuMusicLibrary&) = delete;
    MenuMusicLibrary& operator=(const MenuMusicLibrary&) = delete;

    void ensureDirs();
    void repairDownloadedCoversAsync();
    void notifyChanged();
    void rebuildTrackIndex();
    void markDirty();
    void captureDownloadedSongBatch(std::shared_ptr<DownloadedSongSnapshot> const& snapshot);
    void finishDownloadedSongCapture(std::shared_ptr<DownloadedSongSnapshot> const& snapshot);
    void restartDownloadedSongSync();
    std::size_t mergeDownloadedSongs(std::vector<MusicTrack> candidates, bool notifyWhenUnchanged = false);
// snapshot of the last successfully parsed library.json.
    void backupLastValidLibrary();

    std::vector<MusicTrack> m_tracks;
    std::unordered_map<std::string, size_t> m_trackIndex;
    std::vector<MusicPlaylist> m_playlists;
    PlaybackMode m_mode = PlaybackMode::Disabled;
    std::string m_activePlaylistId;
    std::string m_lastTrackId;
    std::uint64_t m_idCounter = 0;
    // known-song metadata count; songStateChanged invalidates completed downloads.
    std::size_t m_syncedSongCount = static_cast<std::size_t>(-1);
    cocos2d::CCDictionary* m_syncedSongDictionary = nullptr; // identity only; never dereferenced
    std::uint64_t m_downloadSyncGeneration = 0;
    bool m_downloadSyncPending = false;
    bool m_loaded = false;
// set when load() cannot parse the on-disk file: save() must not persist
// the resulting empty in-memory state over it.
    bool m_loadFailed = false;
    bool m_savePending = false;

    std::vector<std::pair<std::size_t, Listener>> m_listeners;
    std::size_t m_nextListenerToken = 1;
};

}
