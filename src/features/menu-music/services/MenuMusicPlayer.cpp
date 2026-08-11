#include "MenuMusicPlayer.hpp"
#include "MenuMusicEffects.hpp"
#include "MenuMusicLibrary.hpp"

#include "../../menu-loop/services/MenuLoopManager.hpp"
#include "../../../utils/MusicChannel.hpp"

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

std::vector<std::string> MenuMusicPlayer::candidateTrackIds() const {
    auto& lib = MenuMusicLibrary::get();
    std::vector<std::string> out;

    const auto& blacklist = paimon::menuloop::MenuLoopManager::get().getBlacklist();
    auto isBlacklisted = [&](const MusicTrack& track) {
        return track.blacklisted || (!track.audioPath.empty()
            && std::find(blacklist.begin(), blacklist.end(), track.audioPath) != blacklist.end());
    };
    auto isPlayable = [](const MusicTrack& track) {
        std::error_code ec;
        return !track.audioPath.empty()
            && std::filesystem::is_regular_file(track.audioPath, ec) && !ec;
    };

    const auto mode = lib.mode();
    if (mode == PlaybackMode::Playlist) {
        if (auto* pl = lib.findPlaylist(lib.activePlaylistId())) {
            for (const auto& tid : pl->trackIds) {
                auto* t = lib.findTrack(tid);
                if (t && isPlayable(*t) && !isBlacklisted(*t)) {
                    out.push_back(tid);
                    if (t->favorite) out.push_back(tid);
                }
            }
        }
    } else if (mode == PlaybackMode::Library || mode == PlaybackMode::Queue) {
        for (const auto& t : lib.tracks()) {
            if (isPlayable(t) && !isBlacklisted(t)) {
                out.push_back(t.id);
                if (t.favorite) out.push_back(t.id);
            }
        }
    }
    return out;
}

std::string MenuMusicPlayer::pickRandomExcept(const std::string& exceptId) const {
    auto ids = candidateTrackIds();
    if (ids.empty()) return "";
    if (std::ranges::all_of(ids, [&](const std::string& id) { return id == ids.front(); })) {
        return ids.front();
    }

    std::erase(ids, exceptId);
    if (ids.empty()) return exceptId;

    static std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<std::size_t> dist(0, ids.size() - 1);

    return ids[dist(rng)];
}

bool MenuMusicPlayer::applyOverrideAndPlay(const std::string& trackId,
                                           const std::string& audioPath) {
    std::error_code existsEc;
    if (audioPath.empty() || !std::filesystem::exists(audioPath, existsEc) || existsEc) {
        log::warn("[MenuMusic] audio file missing for track {}: '{}'",
            trackId, audioPath);
        return false;
    }

    auto& loop = paimon::menuloop::MenuLoopManager::get();
    loop.setOverride(audioPath);
    loop.setCurrentSong(audioPath);
    loop.setCurrentSongDisplayName(
        MenuMusicLibrary::get().findTrack(trackId)
            ? MenuMusicLibrary::get().findTrack(trackId)->displayName
            : geode::utils::string::pathToString(std::filesystem::path(audioPath).stem()));

    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->stop();
    }
    paimon::audio::clearMusicGroupPause();
    GameManager::sharedState()->playMenuMusic();

    m_state.currentTrackId = trackId;
    m_state.currentAudioPath = audioPath;
    m_state.mode = MenuMusicLibrary::get().mode();
    m_state.isPlaying = true;
    m_paused = false;
    m_pausedChannel = nullptr;
    MenuMusicLibrary::get().setLastTrackId(trackId);

    if (!trackId.empty()) {
        if (m_history.empty() || m_history.back() != trackId) {
            m_history.push_back(trackId);
            while (m_history.size() > kMaxHistory) m_history.pop_front();
        }
    }

    MenuMusicEffects::get().activateForCurrentTrack();
    notifyChanged();
    return true;
}

bool MenuMusicPlayer::playNext() {
    auto& lib = MenuMusicLibrary::get();
    if (lib.mode() == PlaybackMode::Disabled) return false;

    auto pick = pickRandomExcept(m_state.currentTrackId);
    if (pick.empty()) return false;

    auto* track = lib.findTrack(pick);
    if (!track) return false;

    return applyOverrideAndPlay(track->id, track->audioPath);
}

bool MenuMusicPlayer::playPrevious() {
    if (m_history.size() < 2) return false;
    m_history.pop_back();
    auto prevId = m_history.back();
    auto& lib = MenuMusicLibrary::get();
    auto* track = lib.findTrack(prevId);
    if (!track) {
        m_history.pop_back();
        return false;
    }
    return applyOverrideAndPlay(track->id, track->audioPath);
}

bool MenuMusicPlayer::playSpecific(const std::string& trackId) {
    auto& lib = MenuMusicLibrary::get();
    auto* track = lib.findTrack(trackId);
    if (!track || track->blacklisted) return false;

    // Cambiar a modo Queue para que el siguiente tick no sobreescriba.
    if (lib.mode() == PlaybackMode::Disabled) {
        lib.setMode(PlaybackMode::Queue);
    }
    return applyOverrideAndPlay(track->id, track->audioPath);
}

bool MenuMusicPlayer::swapHeldTrack() {
    auto* current = currentTrack();
    if (!current) return false;
    const std::string currentId = current->id;

    if (m_heldTrackId.empty()) {
        auto nextId = pickRandomExcept(currentId);
        if (nextId.empty() || nextId == currentId) return false;
        if (!playSpecific(nextId)) return false;
        m_heldTrackId = currentId;
        return true;
    }
    if (m_heldTrackId == currentId) return false;

    const std::string heldId = m_heldTrackId;
    if (!playSpecific(heldId)) return false;
    m_heldTrackId = currentId;
    return true;
}

bool MenuMusicPlayer::isManagingPlayback() const {
    return MenuMusicLibrary::get().mode() != PlaybackMode::Disabled && currentTrack();
}

void MenuMusicPlayer::pause() {
    // Solo el canal donde suena la cancion del menu: m_backgroundMusicChannel
    // es el grupo compartido y pausarlo deja mudo tambien el nivel.
    m_pausedChannel = paimon::audio::mainMusicChannel();
    paimon::audio::setMusicChannelPaused(m_pausedChannel, true);
    m_paused = m_pausedChannel != nullptr;
    m_state.isPlaying = !m_paused;
    notifyChanged();
}

void MenuMusicPlayer::resume() {
    paimon::audio::setMusicChannelPaused(m_pausedChannel, false);
    m_pausedChannel = nullptr;
    m_paused = false;
    m_state.isPlaying = true;
    notifyChanged();
}

bool MenuMusicPlayer::isPaused() const {
    // El juego rearranca la musica del menu en un canal nuevo al salir de un
    // nivel o cambiar de escena; la pausa que aplicamos murio con el anterior.
    return m_paused && paimon::audio::isMusicChannelPaused(m_pausedChannel);
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
    paimon::audio::clearMusicGroupPause();
    GameManager::sharedState()->playMenuMusic();

    m_state.currentTrackId.clear();
    m_state.currentAudioPath.clear();
    m_state.isPlaying = true;
    m_paused = false;
    m_pausedChannel = nullptr;
    MenuMusicEffects::get().deactivate();
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

void MenuMusicPlayer::forgetCurrentTrack() {
    m_state.currentTrackId.clear();
    m_state.currentAudioPath.clear();
    MenuMusicEffects::get().deactivate();
    notifyChanged();
}

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
