#include "MenuMusicLibrary.hpp"

#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

MenuMusicLibrary& MenuMusicLibrary::get() {
    static MenuMusicLibrary instance;
    return instance;
}

MenuMusicLibrary::MenuMusicLibrary() {
    ensureDirs();
}

void MenuMusicLibrary::ensureDirs() {
    std::error_code ec;
    std::filesystem::create_directories(getTracksDir(), ec);
    std::filesystem::create_directories(getCoversDir(), ec);
}

std::filesystem::path MenuMusicLibrary::getRootDir() const {
    return Mod::get()->getSaveDir() / "menu-music";
}
std::filesystem::path MenuMusicLibrary::getTracksDir() const {
    return getRootDir() / "tracks";
}
std::filesystem::path MenuMusicLibrary::getCoversDir() const {
    return getRootDir() / "covers";
}
std::filesystem::path MenuMusicLibrary::getLibraryFile() const {
    return getRootDir() / "library.json";
}


void MenuMusicLibrary::rebuildTrackIndex() {
    m_trackIndex.clear();
    m_trackIndex.reserve(m_tracks.size());
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        m_trackIndex[m_tracks[i].id] = i;
    }
}

void MenuMusicLibrary::markDirty() {
    if (m_savePending) return;
    m_savePending = true;
    Loader::get()->queueInMainThread([this] {
        if (m_savePending) {
            m_savePending = false;
            save();
        }
    });
}


MusicTrack* MenuMusicLibrary::findTrack(const std::string& id) {
    auto it = m_trackIndex.find(id);
    if (it != m_trackIndex.end() && it->second < m_tracks.size()) {
        return &m_tracks[it->second];
    }
    return nullptr;
}

const MusicTrack* MenuMusicLibrary::findTrack(const std::string& id) const {
    auto it = m_trackIndex.find(id);
    if (it != m_trackIndex.end() && it->second < m_tracks.size()) {
        return &m_tracks[it->second];
    }
    return nullptr;
}

MusicTrack* MenuMusicLibrary::findTrackByAudioPath(const std::string& path) {
    auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](const MusicTrack& track) {
        return track.audioPath == path;
    });
    return it == m_tracks.end() ? nullptr : &*it;
}

const MusicTrack* MenuMusicLibrary::findTrackByAudioPath(const std::string& path) const {
    auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](const MusicTrack& track) {
        return track.audioPath == path;
    });
    return it == m_tracks.end() ? nullptr : &*it;
}

void MenuMusicLibrary::addTrack(const MusicTrack& track) {
    if (auto* existing = findTrack(track.id)) {
        bool favorite = existing->favorite;
        bool blacklisted = existing->blacklisted;
        *existing = track;
        existing->favorite = favorite;
        existing->blacklisted = blacklisted;
        markDirty();
        notifyChanged();
        return;
    }
    if (auto* existing = findTrackByAudioPath(track.audioPath)) {
        if (existing->displayName.empty()) existing->displayName = track.displayName;
        if (existing->artist.empty()) existing->artist = track.artist;
        if (existing->coverPath.empty()) existing->coverPath = track.coverPath;
        markDirty();
        notifyChanged();
        return;
    }
    m_trackIndex[track.id] = m_tracks.size();
    m_tracks.push_back(track);
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setFavorite(const std::string& id, bool favorite) {
    auto* track = findTrack(id);
    if (!track || (track->favorite == favorite && (!favorite || !track->blacklisted))) return;
    track->favorite = favorite;
    if (favorite) track->blacklisted = false;
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setBlacklisted(const std::string& id, bool blacklisted) {
    auto* track = findTrack(id);
    if (!track || (track->blacklisted == blacklisted && (!blacklisted || !track->favorite))) return;
    track->blacklisted = blacklisted;
    if (blacklisted) track->favorite = false;
    markDirty();
    notifyChanged();
}

std::size_t MenuMusicLibrary::importFolder(const std::filesystem::path& folder, bool recursive) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec) || ec) return 0;

    std::vector<std::filesystem::path> files;
    auto collect = [&](const auto& entry) {
        std::error_code typeEc;
        if (entry.is_regular_file(typeEc) && !typeEc && isAudioExtension(entry.path())) {
            files.push_back(entry.path());
        }
    };

    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            collect(entry);
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(
                 folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            collect(entry);
        }
    }

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::size_t added = 0;
    for (const auto& path : files) {
        auto normalized = geode::utils::string::pathToString(path);
        if (findTrackByAudioPath(normalized)) continue;

        MusicTrack track;
        track.id = generateId("folder");
        track.audioPath = normalized;
        track.displayName = geode::utils::string::pathToString(path.stem());
        track.source = TrackSource::Local;
        track.addedUnixMs = now;
        m_trackIndex[track.id] = m_tracks.size();
        m_tracks.push_back(std::move(track));
        ++added;
    }

    if (added) {
        markDirty();
        notifyChanged();
    }
    return added;
}

std::size_t MenuMusicLibrary::syncDownloadedSongs(bool force) {
    auto* manager = MusicDownloadManager::sharedState();
    auto* songs = manager ? manager->getDownloadedSongs() : nullptr;
    if (!manager || !songs) return 0;

    // The scan does one stat() per downloaded song, which adds up on every
    // MenuLayer entry. Callers that only want to pick up new downloads can pass
    // force=false to skip it while the downloaded set is unchanged.
    const auto songCount = static_cast<std::size_t>(songs->count());
    if (!force && m_syncedSongCount == songCount) return 0;
    m_syncedSongCount = songCount;

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::size_t added = 0;
    bool changed = false;
    for (auto* song : CCArrayExt<SongInfoObject*>(songs)) {
        if (!song || manager->isResourceSong(song->m_songID)) continue;

        std::string audioPath = manager->pathForSong(song->m_songID);
        std::error_code existsEc;
        if (!isAudioExtension(audioPath) ||
            !std::filesystem::is_regular_file(audioPath, existsEc) || existsEc) {
            continue;
        }

        if (auto* existing = findTrackByAudioPath(audioPath)) {
            if (existing->displayName.empty() && !song->m_songName.empty()) {
                existing->displayName = song->m_songName;
                changed = true;
            }
            if (existing->artist.empty() && !song->m_artistName.empty()) {
                existing->artist = song->m_artistName;
                changed = true;
            }
            continue;
        }

        MusicTrack track;
        track.id = fmt::format("gd_{}", song->m_songID);
        if (findTrack(track.id)) track.id = generateId("gd");
        track.audioPath = audioPath;
        track.displayName = song->m_songName;
        track.artist = song->m_artistName;
        track.source = TrackSource::GeometryDash;
        track.addedUnixMs = now;
        m_trackIndex[track.id] = m_tracks.size();
        m_tracks.push_back(std::move(track));
        ++added;
    }

    if (added || changed) {
        markDirty();
        notifyChanged();
    }
    return added;
}

void MenuMusicLibrary::updateTrack(const MusicTrack& track) {
    if (auto* existing = findTrack(track.id)) {
        *existing = track;
        markDirty();
        notifyChanged();
    }
}

bool MenuMusicLibrary::deleteLocalAudio(const std::string& id) {
    auto* track = findTrack(id);
    if (!track || track->audioPath.empty()) return false;

    std::error_code ec;
    if (track->source == TrackSource::Downloaded) {
        std::filesystem::remove(track->audioPath, ec);
    } else if (track->source == TrackSource::GeometryDash) {
        auto stem = geode::utils::string::pathToString(
            std::filesystem::path(track->audioPath).stem());
        int songId = 0;
        auto [end, parseError] = std::from_chars(
            stem.data(), stem.data() + stem.size(), songId);
        if (parseError != std::errc{} || end != stem.data() + stem.size() || songId <= 0) {
            return false;
        }

        if (auto* manager = MusicDownloadManager::sharedState()) {
            manager->deleteSong(songId);
            if (std::filesystem::is_regular_file(track->audioPath, ec) && !ec) {
                std::filesystem::remove(track->audioPath, ec);
            }
        } else {
            std::filesystem::remove(track->audioPath, ec);
        }
    } else {
        return false;
    }

    if (ec) return false;
    notifyChanged();
    return true;
}

void MenuMusicLibrary::removeTrack(const std::string& id, bool deleteFiles) {
    auto* existing = findTrack(id);
    if (!existing) return;

    MusicTrack copy = *existing;

    m_tracks.erase(
        std::remove_if(m_tracks.begin(), m_tracks.end(),
            [&](const MusicTrack& t) { return t.id == id; }),
        m_tracks.end());
    rebuildTrackIndex();

    for (auto& pl : m_playlists) {
        pl.trackIds.erase(
            std::remove(pl.trackIds.begin(), pl.trackIds.end(), id),
            pl.trackIds.end());
    }

    if (m_lastTrackId == id) m_lastTrackId.clear();

    if (deleteFiles && copy.source == TrackSource::Downloaded) {
        std::error_code ec;
        if (!copy.audioPath.empty()) {
            std::filesystem::remove(copy.audioPath, ec);
        }
        if (!copy.coverPath.empty()) {
            std::filesystem::remove(copy.coverPath, ec);
        }
    }

    markDirty();
    notifyChanged();
}


MusicPlaylist* MenuMusicLibrary::findPlaylist(const std::string& id) {
    for (auto& p : m_playlists) if (p.id == id) return &p;
    return nullptr;
}

void MenuMusicLibrary::addPlaylist(const MusicPlaylist& pl) {
    m_playlists.push_back(pl);
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::renamePlaylist(const std::string& id, const std::string& newName) {
    if (auto* pl = findPlaylist(id)) {
        pl->name = newName;
        markDirty();
        notifyChanged();
    }
}

void MenuMusicLibrary::removePlaylist(const std::string& id) {
    m_playlists.erase(
        std::remove_if(m_playlists.begin(), m_playlists.end(),
            [&](const MusicPlaylist& p) { return p.id == id; }),
        m_playlists.end());
    if (m_activePlaylistId == id) m_activePlaylistId.clear();
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::addTrackToPlaylist(const std::string& playlistId, const std::string& trackId) {
    auto* pl = findPlaylist(playlistId);
    if (!pl) return;
    if (std::find(pl->trackIds.begin(), pl->trackIds.end(), trackId) != pl->trackIds.end()) return;
    pl->trackIds.push_back(trackId);
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::removeTrackFromPlaylist(const std::string& playlistId, const std::string& trackId) {
    auto* pl = findPlaylist(playlistId);
    if (!pl) return;
    auto before = pl->trackIds.size();
    pl->trackIds.erase(
        std::remove(pl->trackIds.begin(), pl->trackIds.end(), trackId),
        pl->trackIds.end());
    if (pl->trackIds.size() != before) {
        markDirty();
        notifyChanged();
    }
}


void MenuMusicLibrary::setMode(PlaybackMode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setActivePlaylistId(const std::string& id) {
    if (m_activePlaylistId == id) return;
    m_activePlaylistId = id;
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::setLastTrackId(const std::string& id) {
    if (m_lastTrackId == id) return;
    m_lastTrackId = id;
    markDirty();
}


std::string MenuMusicLibrary::generateId(const std::string& prefix) {
    m_idCounter++;
    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return fmt::format("{}_{}_{}", prefix, m_idCounter, stamp);
}

bool MenuMusicLibrary::isAudioExtension(const std::filesystem::path& p) {
    auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(p.extension()));
    // Formatos aceptados por FMOD 2.02+ (el que empaqueta GD 2.2):
    //   * mp3, ogg-vorbis, wav, flac, m4a (AAC en MP4), ogg-opus (.opus)
    // Nota: no incluimos .webm porque FMOD decodifica Opus SOLO dentro
    // de contenedor Ogg (.opus/.oga), no de WebM. El downloader remuxea
    // al contenedor correcto cuando se pide el formato Opus.
    static const std::array<std::string, 7> ok = {
        ".mp3", ".ogg", ".wav", ".flac", ".oga", ".m4a", ".opus"
    };
    return std::find(ok.begin(), ok.end(), ext) != ok.end();
}

bool MenuMusicLibrary::isImageExtension(const std::filesystem::path& p) {
    auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(p.extension()));
    static const std::array<std::string, 6> ok = {
        ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tiff"
    };
    return std::find(ok.begin(), ok.end(), ext) != ok.end();
}


std::size_t MenuMusicLibrary::addListener(Listener cb) {
    auto token = m_nextListenerToken++;
    m_listeners.emplace_back(token, std::move(cb));
    return token;
}

void MenuMusicLibrary::removeListener(std::size_t token) {
    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [&](const auto& p) { return p.first == token; }),
        m_listeners.end());
}

void MenuMusicLibrary::notifyChanged() {
    // Copia defensiva para permitir que un listener se desregistre a si mismo
    auto copy = m_listeners;
    for (auto& [token, cb] : copy) {
        if (cb) cb();
    }
}


void MenuMusicLibrary::save() {
    ensureDirs();

    auto root = matjson::Value::object();
    root["version"] = 2;
    root["mode"] = static_cast<int>(m_mode);
    root["activePlaylistId"] = m_activePlaylistId;
    root["lastTrackId"] = m_lastTrackId;
    root["idCounter"] = static_cast<std::int64_t>(m_idCounter);

    auto tracks = matjson::Value::array();
    for (const auto& t : m_tracks) {
        matjson::Value o = matjson::Value::object();
        o["id"] = t.id;
        o["audioPath"] = t.audioPath;
        o["coverPath"] = t.coverPath;
        o["displayName"] = t.displayName;
        o["artist"] = t.artist;
        o["sourceUrl"] = t.sourceUrl;
        o["source"] = static_cast<int>(t.source);
        o["addedUnixMs"] = t.addedUnixMs;
        o["durationMs"] = t.durationMs;
        o["favorite"] = t.favorite;
        o["blacklisted"] = t.blacklisted;
        tracks.push(o);
    }
    root["tracks"] = tracks;

    auto playlists = matjson::Value::array();
    for (const auto& p : m_playlists) {
        matjson::Value o = matjson::Value::object();
        o["id"] = p.id;
        o["name"] = p.name;
        o["createdUnixMs"] = p.createdUnixMs;
        auto ids = matjson::Value::array();
        for (const auto& tid : p.trackIds) ids.push(tid);
        o["trackIds"] = ids;
        playlists.push(o);
    }
    root["playlists"] = playlists;

    (void)file::writeToJson(getLibraryFile(), root);
}

void MenuMusicLibrary::load() {
    if (m_loaded) return;
    m_loaded = true;
    ensureDirs();

    std::error_code existsEc;
    if (!std::filesystem::exists(getLibraryFile(), existsEc) || existsEc) {
        save();
        return;
    }

    auto res = file::readFromJson<matjson::Value>(getLibraryFile());
    if (!res) {
        log::warn("[MenuMusic] failed to read library.json: {}", res.unwrapErr());
        return;
    }
    auto& root = res.unwrap();

    m_mode = static_cast<PlaybackMode>(root["mode"].asInt().unwrapOr(0));
    m_activePlaylistId = root["activePlaylistId"].asString().unwrapOr("");
    m_lastTrackId = root["lastTrackId"].asString().unwrapOr("");
    m_idCounter = static_cast<std::uint64_t>(root["idCounter"].asInt().unwrapOr(0));

    m_tracks.clear();
    if (auto arr = root["tracks"].asArray()) {
        for (const auto& item : arr.unwrap()) {
            MusicTrack t;
            t.id = item["id"].asString().unwrapOr("");
            if (t.id.empty()) continue;
            t.audioPath = item["audioPath"].asString().unwrapOr("");
            t.coverPath = item["coverPath"].asString().unwrapOr("");
            t.displayName = item["displayName"].asString().unwrapOr("");
            t.artist = item["artist"].asString().unwrapOr("");
            t.sourceUrl = item["sourceUrl"].asString().unwrapOr("");
            t.source = static_cast<TrackSource>(item["source"].asInt().unwrapOr(0));
            t.addedUnixMs = item["addedUnixMs"].asInt().unwrapOr(0);
            t.durationMs = static_cast<std::int32_t>(item["durationMs"].asInt().unwrapOr(0));
            t.favorite = item["favorite"].asBool().unwrapOr(false);
            t.blacklisted = item["blacklisted"].asBool().unwrapOr(false);
            m_tracks.push_back(std::move(t));
        }
    }

    m_playlists.clear();
    if (auto arr = root["playlists"].asArray()) {
        for (const auto& item : arr.unwrap()) {
            MusicPlaylist p;
            p.id = item["id"].asString().unwrapOr("");
            if (p.id.empty()) continue;
            p.name = item["name"].asString().unwrapOr("Untitled");
            p.createdUnixMs = item["createdUnixMs"].asInt().unwrapOr(0);
            if (auto ids = item["trackIds"].asArray()) {
                for (const auto& it : ids.unwrap()) {
                    p.trackIds.push_back(it.asString().unwrapOr(""));
                }
            }
            m_playlists.push_back(std::move(p));
        }
    }

    rebuildTrackIndex();

    log::info("[MenuMusic] library loaded: {} tracks, {} playlists, mode={}",
        m_tracks.size(), m_playlists.size(), static_cast<int>(m_mode));
}

} // namespace paimon::menumusic
