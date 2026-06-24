#include "MenuMusicPlayer.hpp"
#include "MenuMusicLibrary.hpp"

#include "../../menu-loop/services/MenuLoopManager.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/loader/Loader.hpp>
#include <algorithm>
#include <filesystem>
#include <random>

using namespace geode::prelude;

namespace paimon::menumusic {

MenuMusicPlayer& MenuMusicPlayer::get() {
    static MenuMusicPlayer instance;
    return instance;
}

// Seleccion

std::vector<std::string> MenuMusicPlayer::candidateTrackIds() const {
    auto& lib = MenuMusicLibrary::get();
    std::vector<std::string> out;

    const auto mode = lib.mode();
    if (mode == PlaybackMode::Playlist) {
        if (auto* pl = lib.findPlaylist(lib.activePlaylistId())) {
            for (const auto& tid : pl->trackIds) {
                if (lib.findTrack(tid)) out.push_back(tid);
            }
        }
    } else if (mode == PlaybackMode::Library || mode == PlaybackMode::Queue) {
        for (const auto& t : lib.tracks()) out.push_back(t.id);
    }
    return out;
}

std::string MenuMusicPlayer::pickRandomExcept(const std::string& exceptId) const {
    auto ids = candidateTrackIds();
    if (ids.empty()) return "";
    if (ids.size() == 1) return ids.front();

    // Random con reintentos finitos para evitar repetir inmediato.
    static std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<std::size_t> dist(0, ids.size() - 1);

    std::string pick = ids[dist(rng)];
    for (int tries = 0; tries < 5 && pick == exceptId; ++tries) {
        pick = ids[dist(rng)];
    }
    return pick;
}

// Helper: aplicar override e iniciar playback

void MenuMusicPlayer::applyOverrideAndPlay(const std::string& trackId,
                                           const std::string& audioPath) {
    std::error_code existsEc;
    if (audioPath.empty() || !std::filesystem::exists(audioPath, existsEc) || existsEc) {
        log::warn("[MenuMusic] audio file missing for track {}: '{}'",
            trackId, audioPath);
        return;
    }

    // Empujar al sistema existente de override en MenuLoopManager.
    auto& loop = paimon::menuloop::MenuLoopManager::get();
    loop.setOverride(audioPath);
    loop.setCurrentSong(audioPath);
    loop.setCurrentSongDisplayName(
        MenuMusicLibrary::get().findTrack(trackId)
            ? MenuMusicLibrary::get().findTrack(trackId)->displayName
            : geode::utils::string::pathToString(std::filesystem::path(audioPath).stem()));

    // Detener lo que este sonando y replay.
    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->stop();
    }
    GameManager::sharedState()->playMenuMusic();

    // Actualizar estado local.
    m_state.currentTrackId = trackId;
    m_state.currentAudioPath = audioPath;
    m_state.mode = MenuMusicLibrary::get().mode();
    m_state.isPlaying = true;
    m_paused = false;

    // Historial.
    if (!trackId.empty()) {
        if (m_history.empty() || m_history.back() != trackId) {
            m_history.push_back(trackId);
            while (m_history.size() > kMaxHistory) m_history.pop_front();
        }
    }

    notifyChanged();
}

// API publica

bool MenuMusicPlayer::playNext() {
    auto& lib = MenuMusicLibrary::get();
    if (lib.mode() == PlaybackMode::Disabled) return false;

    auto pick = pickRandomExcept(m_state.currentTrackId);
    if (pick.empty()) return false;

    auto* track = lib.findTrack(pick);
    if (!track) return false;

    applyOverrideAndPlay(track->id, track->audioPath);
    return true;
}

bool MenuMusicPlayer::playPrevious() {
    if (m_history.size() < 2) return false;
    // pop del actual, coger el anterior
    m_history.pop_back();
    auto prevId = m_history.back();
    auto& lib = MenuMusicLibrary::get();
    auto* track = lib.findTrack(prevId);
    if (!track) {
        m_history.pop_back();
        return false;
    }
    applyOverrideAndPlay(track->id, track->audioPath);
    return true;
}

bool MenuMusicPlayer::playSpecific(const std::string& trackId) {
    auto& lib = MenuMusicLibrary::get();
    auto* track = lib.findTrack(trackId);
    if (!track) return false;

    // Cambiar a modo Queue para que el siguiente tick no sobreescriba.
    if (lib.mode() == PlaybackMode::Disabled) {
        lib.setMode(PlaybackMode::Queue);
    }
    applyOverrideAndPlay(track->id, track->audioPath);
    return true;
}

void MenuMusicPlayer::pause() {
    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->setPaused(true);
    }
    m_paused = true;
    m_state.isPlaying = false;
    notifyChanged();
}

void MenuMusicPlayer::resume() {
    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->setPaused(false);
    }
    m_paused = false;
    m_state.isPlaying = true;
    notifyChanged();
}

void MenuMusicPlayer::toggleVanillaFallback() {
    auto& loop = paimon::menuloop::MenuLoopManager::get();
    loop.setOverride("");
    loop.setCurrentSongDisplayName("");
    loop.setCurrentSong("menuLoop.mp3");

    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->stop();
    }
    GameManager::sharedState()->playMenuMusic();

    m_state.currentTrackId.clear();
    m_state.currentAudioPath.clear();
    m_state.isPlaying = true;
    m_paused = false;
    notifyChanged();
}

void MenuMusicPlayer::setMode(PlaybackMode mode, bool playNow) {
    auto& lib = MenuMusicLibrary::get();
    lib.setMode(mode);
    if (mode == PlaybackMode::Disabled) {
        toggleVanillaFallback();
        return;
    }
    if (playNow) {
        playNext();
    }
}

const MusicTrack* MenuMusicPlayer::currentTrack() const {
    if (m_state.currentTrackId.empty()) return nullptr;
    return MenuMusicLibrary::get().findTrack(m_state.currentTrackId);
}

// Listeners

std::size_t MenuMusicPlayer::addListener(TrackChangedListener cb) {
    auto token = m_nextListenerToken++;
    m_listeners.emplace_back(token, std::move(cb));
    return token;
}

void MenuMusicPlayer::removeListener(std::size_t token) {
    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [&](const auto& p) { return p.first == token; }),
        m_listeners.end());
}

void MenuMusicPlayer::notifyChanged() {
    auto copy = m_listeners;
    for (auto& [token, cb] : copy) {
        if (cb) cb(m_state.currentTrackId);
    }
}

} // namespace paimon::menumusic
