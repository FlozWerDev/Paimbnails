#pragma once

// MenuMusicLibrary — almacen persistente de tracks y playlists.
//
// Responsabilidades:
//   * CRUD de tracks y playlists.
//   * Persistencia en [saveDir]/menu-music/library.json.
//   * Generacion de ids unicos.
//   * No sabe nada de FMOD ni de UI (lo usa MenuMusicPlayer arriba).
//
// Convencion: todos los paths son absolutos y normalizados con
// `geode::utils::string::pathToString` antes de guardarse, para que
// sobrevivan al round-trip Windows <-> UTF-8.

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

    // Directorios
    std::filesystem::path getRootDir() const;
    std::filesystem::path getTracksDir() const;
    std::filesystem::path getCoversDir() const;
    std::filesystem::path getLibraryFile() const;

    // Ciclo de vida
    void load();                     // idempotente
    void save();
    bool isLoaded() const { return m_loaded; }

    // Tracks
    std::vector<MusicTrack>& tracks() { return m_tracks; }
    const std::vector<MusicTrack>& tracks() const { return m_tracks; }
    MusicTrack* findTrack(const std::string& id);
    const MusicTrack* findTrack(const std::string& id) const;
    void addTrack(const MusicTrack& track);
    void updateTrack(const MusicTrack& track);
    // removeTrack: if deleteFiles=true, also deletes audio+cover from disk
    // (only for Downloaded tracks; Local tracks are left untouched).
    void removeTrack(const std::string& id, bool deleteFiles);
    bool hasTracks() const { return !m_tracks.empty(); }

    // Playlists
    std::vector<MusicPlaylist>& playlists() { return m_playlists; }
    const std::vector<MusicPlaylist>& playlists() const { return m_playlists; }
    MusicPlaylist* findPlaylist(const std::string& id);
    void addPlaylist(const MusicPlaylist& pl);
    void renamePlaylist(const std::string& id, const std::string& newName);
    void removePlaylist(const std::string& id);
    void addTrackToPlaylist(const std::string& playlistId, const std::string& trackId);
    void removeTrackFromPlaylist(const std::string& playlistId, const std::string& trackId);

    // Modo activo
    PlaybackMode mode() const { return m_mode; }
    void setMode(PlaybackMode mode);

    std::string activePlaylistId() const { return m_activePlaylistId; }
    void setActivePlaylistId(const std::string& id);

    // Util
    std::string generateId(const std::string& prefix = "trk");
    static bool isAudioExtension(const std::filesystem::path& p);
    static bool isImageExtension(const std::filesystem::path& p);

    // Notify: se dispara tras cualquier cambio persistido para que la UI
    // abierta (popup / library) se refresque. Reentrante.
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
    std::uint64_t m_idCounter = 0;
    bool m_loaded = false;
    bool m_savePending = false;

    std::vector<std::pair<std::size_t, Listener>> m_listeners;
    std::size_t m_nextListenerToken = 1;
};

} // namespace paimon::menumusic
