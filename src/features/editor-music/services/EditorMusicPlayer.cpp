#include "EditorMusicPlayer.hpp"
#include "../../menu-music/services/MenuMusicLibrary.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>
#include <system_error>

using namespace geode::prelude;
using paimon::menumusic::MenuMusicLibrary;

namespace paimon::editormusic {

// Alias local: evita el choque con geode::PlaybackMode (Geode/Enums.hpp)
// que entra via `using namespace geode::prelude`.
using PlaybackMode = paimon::menumusic::PlaybackMode;

EditorMusicPlayer& EditorMusicPlayer::get() {
    static EditorMusicPlayer instance;
    return instance;
}

bool EditorMusicPlayer::ensureGroup() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;
    if (m_group) return true;

    FMOD_RESULT r = engine->m_system->createChannelGroup("PaimonEditorMusic", &m_group);
    if (r != FMOD_OK || !m_group) {
        m_group = nullptr;
        return false;
    }
    FMOD::ChannelGroup* master = nullptr;
    if (engine->m_system->getMasterChannelGroup(&master) == FMOD_OK && master) {
        master->addGroup(m_group);
    }
    return true;
}

std::vector<std::string> EditorMusicPlayer::candidateTrackIds() const {
    auto& lib = MenuMusicLibrary::get();
    std::vector<std::string> out;
    if (m_mode == PlaybackMode::Playlist) {
        if (auto* pl = lib.findPlaylist(m_activePlaylistId)) {
            for (const auto& tid : pl->trackIds) {
                if (lib.findTrack(tid)) out.push_back(tid);
            }
        }
    } else {
        for (const auto& t : lib.tracks()) out.push_back(t.id);
    }
    return out;
}

bool EditorMusicPlayer::playTrackById(const std::string& trackId) {
    auto* track = MenuMusicLibrary::get().findTrack(trackId);
    if (!track) return false;

    std::error_code ec;
    if (track->audioPath.empty() || !std::filesystem::exists(track->audioPath, ec) || ec) {
        log::warn("[EditorMusic] missing audio for track {}", trackId);
        return false;
    }
    if (!ensureGroup()) return false;

    auto* engine = FMODAudioEngine::sharedEngine();

    // Liberar el sonido previo antes de crear el nuevo.
    if (m_channel) { m_channel->stop(); m_channel = nullptr; }
    if (m_sound) { m_sound->release(); m_sound = nullptr; }

    FMOD::Sound* sound = nullptr;
    FMOD_RESULT r = engine->m_system->createSound(
        track->audioPath.c_str(), FMOD_CREATESTREAM | FMOD_LOOP_NORMAL, nullptr, &sound);
    if (r != FMOD_OK || !sound) return false;
    m_sound = sound;

    FMOD::Channel* ch = nullptr;
    r = engine->m_system->playSound(m_sound, m_group, false, &ch);
    if (r != FMOD_OK || !ch) {
        m_sound->release();
        m_sound = nullptr;
        return false;
    }
    m_channel = ch;
    m_channel->setVolume(engine->m_musicVolume);

    m_currentTrackId = trackId;
    m_currentDisplayName = track->displayName.empty()
        ? geode::utils::string::pathToString(std::filesystem::path(track->audioPath).stem())
        : track->displayName;
    m_paused = false;
    m_channel->setPaused(m_mainActive);  // arranca pausado si el canal principal suena

    if (m_history.empty() || m_history.back() != trackId) {
        m_history.push_back(trackId);
        while (m_history.size() > kMaxHistory) m_history.pop_front();
    }
    return true;
}

bool EditorMusicPlayer::playNext() {
    auto ids = candidateTrackIds();
    if (ids.empty()) return false;

    std::string pick = ids.front();
    if (ids.size() > 1) {
        static std::mt19937 rng(static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        std::uniform_int_distribution<std::size_t> dist(0, ids.size() - 1);
        pick = ids[dist(rng)];
        for (int tries = 0; tries < 5 && pick == m_currentTrackId; ++tries) {
            pick = ids[dist(rng)];
        }
    }
    return playTrackById(pick);
}

bool EditorMusicPlayer::playPrevious() {
    if (m_history.size() < 2) return false;
    m_history.pop_back();          // descartar el actual
    auto prevId = m_history.back();
    m_history.pop_back();          // playTrackById lo re-anade
    return playTrackById(prevId);
}

void EditorMusicPlayer::togglePause() {
    if (!m_channel) { playNext(); return; }
    m_paused = !m_paused;
    m_channel->setPaused(m_paused || m_mainActive);
}

void EditorMusicPlayer::setMainChannelActive(bool active) {
    if (active == m_mainActive) return;
    m_mainActive = active;
    if (m_channel) m_channel->setPaused(m_paused || m_mainActive);
}

bool EditorMusicPlayer::isPlaying() const {
    if (!m_channel) return false;
    bool playing = false;
    m_channel->isPlaying(&playing);
    return playing && !m_paused;
}

void EditorMusicPlayer::cycleMode() {
    m_mode = (m_mode == PlaybackMode::Library) ? PlaybackMode::Playlist : PlaybackMode::Library;
    if (m_mode == PlaybackMode::Playlist && m_activePlaylistId.empty()) {
        auto& pls = MenuMusicLibrary::get().playlists();
        if (!pls.empty()) m_activePlaylistId = pls.front().id;
    }
}

void EditorMusicPlayer::cyclePlaylist() {
    auto& pls = MenuMusicLibrary::get().playlists();
    if (pls.empty()) return;
    m_mode = PlaybackMode::Playlist;
    std::size_t idx = 0;
    for (std::size_t i = 0; i < pls.size(); ++i) {
        if (pls[i].id == m_activePlaylistId) { idx = i; break; }
    }
    m_activePlaylistId = pls[(idx + 1) % pls.size()].id;
}

void EditorMusicPlayer::stop() {
    if (m_channel) { m_channel->stop(); m_channel = nullptr; }
    if (m_sound) { m_sound->release(); m_sound = nullptr; }
    m_paused = false;
    m_currentTrackId.clear();
    m_currentDisplayName.clear();
}

std::string EditorMusicPlayer::currentDisplayName() const {
    return m_currentDisplayName.empty() ? "No track" : m_currentDisplayName;
}

std::string EditorMusicPlayer::modeLabel() const {
    if (m_mode == PlaybackMode::Playlist) {
        if (auto* pl = MenuMusicLibrary::get().findPlaylist(m_activePlaylistId)) {
            return "Playlist: " + pl->name;
        }
        return "Playlist";
    }
    return "Library";
}

} // namespace paimon::editormusic
