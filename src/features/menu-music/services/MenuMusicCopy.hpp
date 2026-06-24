#pragma once

#include "../model/MenuMusicTypes.hpp"
#include "MenuMusicPlayer.hpp"
#include "../../menu-loop/services/MenuLoopManager.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace paimon::menumusic {

// Extrae un Newgrounds / GD song ID del stem de un archivo.
// Soporta "851118.mp3" y prefijos tipo "DL_1_851118".
inline std::optional<int> tryParseGDSongIdFromStem(std::string const& stem) {
    if (stem.empty()) return std::nullopt;

    if (auto numRes = geode::utils::numFromString<int>(stem); numRes.isOk()) {
        int id = numRes.unwrap();
        if (id > 0) return id;
    }

    auto lastUnderscore = stem.find_last_of('_');
    if (lastUnderscore != std::string::npos && lastUnderscore + 1 < stem.size()) {
        auto tail = stem.substr(lastUnderscore + 1);
        if (auto idRes = geode::utils::numFromString<int>(tail); idRes.isOk()) {
            int id = idRes.unwrap();
            if (id > 0) return id;
        }
    }

    return std::nullopt;
}

inline std::optional<int> tryParseGDSongIdFromPath(std::string const& path) {
    if (path.empty()) return std::nullopt;
    auto stem = geode::utils::string::pathToString(std::filesystem::path(path).stem());
    return tryParseGDSongIdFromStem(stem);
}

inline std::optional<int> tryParseGDSongIdFromUrl(std::string const& url) {
    constexpr char const* marker = "/audio/listen/";
    auto pos = url.find(marker);
    if (pos == std::string::npos) return std::nullopt;

    auto tail = url.substr(pos + 14);
    auto end = tail.find_first_of("?#/&");
    if (end != std::string::npos) tail = tail.substr(0, end);

    if (auto idRes = geode::utils::numFromString<int>(tail); idRes.isOk()) {
        int id = idRes.unwrap();
        if (id > 0) return id;
    }
    return std::nullopt;
}

inline std::optional<int> resolveGDSongId(
    std::string const& audioPath,
    std::string const& sourceUrl = {}
) {
    if (auto id = tryParseGDSongIdFromPath(audioPath)) return id;
    if (!sourceUrl.empty()) {
        if (auto id = tryParseGDSongIdFromUrl(sourceUrl)) return id;
    }
    return std::nullopt;
}

inline std::string fallbackTrackLabel(MusicTrack const& track) {
    if (!track.displayName.empty()) return track.displayName;
    return geode::utils::string::pathToString(std::filesystem::path(track.audioPath).stem());
}

inline std::string fallbackPathLabel(
    std::string const& path,
    std::string const& displayName = {}
) {
    if (!displayName.empty()) return displayName;
    if (path.empty()) return {};
    return geode::utils::string::pathToString(std::filesystem::path(path).stem());
}

// Valor a copiar al portapapeles: ID si existe, si no el nombre visible.
inline std::string resolveTrackCopyValue(MusicTrack const& track) {
    if (auto id = resolveGDSongId(track.audioPath, track.sourceUrl)) {
        return std::to_string(*id);
    }
    return fallbackTrackLabel(track);
}

inline std::string resolvePathCopyValue(
    std::string const& path,
    std::string const& displayName = {}
) {
    if (path == "menuLoop.mp3" || path.empty()) return "menuLoop.mp3";
    if (auto id = resolveGDSongId(path)) {
        return std::to_string(*id);
    }
    return fallbackPathLabel(path, displayName);
}

inline bool isVanillaMenuLoopPath(std::string const& path) {
    if (path.empty()) return true;
    auto filename = geode::utils::string::pathToString(std::filesystem::path(path).filename());
    return filename == "menuLoop.mp3";
}

// Lo que realmente suena ahora (hook de getMenuMusicFile incluido).
inline std::string resolveActiveMenuMusicPath() {
    if (auto* gm = GameManager::get()) {
        std::string file = gm->getMenuMusicFile();
        if (!file.empty() && !isVanillaMenuLoopPath(file)) {
            return file;
        }
    }

    auto& loop = paimon::menuloop::MenuLoopManager::get();
    if (loop.isOverride()) {
        return loop.getCurrentSong();
    }

    std::string current = loop.getCurrentSong();
    if (!isVanillaMenuLoopPath(current)) {
        return current;
    }
    return {};
}

inline std::string resolveActiveMenuMusicCopyValue() {
    if (auto* track = MenuMusicPlayer::get().currentTrack()) {
        return resolveTrackCopyValue(*track);
    }

    auto& loop = paimon::menuloop::MenuLoopManager::get();
    if (auto path = resolveActiveMenuMusicPath(); !path.empty()) {
        return resolvePathCopyValue(path, loop.getCurrentSongDisplayName());
    }

    return "menuLoop.mp3";
}

} // namespace paimon::menumusic