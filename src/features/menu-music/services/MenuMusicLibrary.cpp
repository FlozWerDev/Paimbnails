#include "MenuMusicLibrary.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <array>
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

// Index

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

// Tracks

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

void MenuMusicLibrary::addTrack(const MusicTrack& track) {
    m_trackIndex[track.id] = m_tracks.size();
    m_tracks.push_back(track);
    markDirty();
    notifyChanged();
}

void MenuMusicLibrary::updateTrack(const MusicTrack& track) {
    if (auto* existing = findTrack(track.id)) {
        *existing = track;
        markDirty();
        notifyChanged();
    }
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

// Playlists

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

// Modo

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

// Util

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

// Listeners

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

// Serializacion

void MenuMusicLibrary::save() {
    ensureDirs();

    auto root = matjson::Value::object();
    root["version"] = 1;
    root["mode"] = static_cast<int>(m_mode);
    root["activePlaylistId"] = m_activePlaylistId;
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
