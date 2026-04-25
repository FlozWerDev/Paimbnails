#include "AudioContextCoordinator.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include "../../../utils/AudioInterop.hpp"
#include "../../../framework/EventBus.hpp"
#include "../../../framework/ModEvents.hpp"

using namespace geode::prelude;

using MainAudioOwner = AudioContextCoordinator::MainAudioOwner;

namespace {
    std::string ownerToString(MainAudioOwner owner) {
        switch (owner) {
            case MainAudioOwner::None:    return "none";
            case MainAudioOwner::Menu:    return "menu";
            case MainAudioOwner::Dynamic: return "dynamic";
            case MainAudioOwner::Profile: return "profile";
            case MainAudioOwner::Preview: return "preview";
            default: return "unknown";
        }
    }

    void publishOwnerChange(MainAudioOwner prev, MainAudioOwner cur, uint32_t token) {
        paimon::EventBus::get().publish(paimon::AudioOwnerChangedEvent{
            ownerToString(prev), ownerToString(cur), static_cast<int>(token)
        });
    }
}

AudioContextCoordinator& AudioContextCoordinator::get() {
    static AudioContextCoordinator instance;
    return instance;
}

void AudioContextCoordinator::claimDynamicAudio() {
    auto prev = m_mainAudioOwner;
    m_mainAudioOwner = MainAudioOwner::Dynamic;
    m_mainAudioOwnerToken = 0;
    if (prev != MainAudioOwner::Dynamic) publishOwnerChange(prev, m_mainAudioOwner, 0);
}

void AudioContextCoordinator::clearDynamicAudio() {
    if (m_mainAudioOwner == MainAudioOwner::Dynamic) {
        m_mainAudioOwner = MainAudioOwner::None;
        m_mainAudioOwnerToken = 0;
        publishOwnerChange(MainAudioOwner::Dynamic, MainAudioOwner::None, 0);
    }
}

void AudioContextCoordinator::claimProfileAudio(uint32_t sessionToken) {
    auto prev = m_mainAudioOwner;
    m_mainAudioOwner = MainAudioOwner::Profile;
    m_mainAudioOwnerToken = sessionToken;
    if (prev != MainAudioOwner::Profile) publishOwnerChange(prev, m_mainAudioOwner, sessionToken);
}

void AudioContextCoordinator::claimPreviewAudio(uint32_t sessionToken) {
    auto prev = m_mainAudioOwner;
    m_mainAudioOwner = MainAudioOwner::Preview;
    m_mainAudioOwnerToken = sessionToken;
    if (prev != MainAudioOwner::Preview) publishOwnerChange(prev, m_mainAudioOwner, sessionToken);
}

void AudioContextCoordinator::releaseProfileLikeAudio(uint32_t sessionToken) {
    if ((m_mainAudioOwner == MainAudioOwner::Profile || m_mainAudioOwner == MainAudioOwner::Preview) &&
        m_mainAudioOwnerToken == sessionToken) {
        auto prev = m_mainAudioOwner;
        m_mainAudioOwner = MainAudioOwner::None;
        m_mainAudioOwnerToken = 0;
        publishOwnerChange(prev, MainAudioOwner::None, sessionToken);
    }
}

bool AudioContextCoordinator::isCurrentProfileSession(uint32_t sessionToken) const {
    return sessionToken == m_profileSessionToken;
}

bool AudioContextCoordinator::isAudioOwnedByProfileSession(uint32_t sessionToken) const {
    return (m_mainAudioOwner == MainAudioOwner::Profile || m_mainAudioOwner == MainAudioOwner::Preview) &&
           m_mainAudioOwnerToken == sessionToken;
}

void AudioContextCoordinator::activateLevelSelect(int levelID, bool playImmediately) {
    m_gameplayActive = false;
    m_levelSelectLevelID = levelID;
    m_dynamicContextLayer = DynSongLayer::LevelSelect;

    if (m_profileOpen) {
        clearProfileContext();
    }

    auto* dsm = DynamicSongManager::get();
    dsm->enterLayer(DynSongLayer::LevelSelect);

    if (playImmediately) {
        playDynamicForCurrentContext();
    }
}

void AudioContextCoordinator::deactivateLevelSelect(bool stopSong) {
    auto* dsm = DynamicSongManager::get();
    dsm->exitLayer(DynSongLayer::LevelSelect);

    if (m_dynamicContextLayer == DynSongLayer::LevelSelect) {
        m_dynamicContextLayer = DynSongLayer::None;
    }

    if (stopSong && !m_profileOpen) {
        dsm->stopSong();
    }
}

void AudioContextCoordinator::activateLevelInfo(GJGameLevel* level, bool playImmediately) {
    m_gameplayActive = false;
    m_levelInfoLevel = level;
    m_dynamicContextLayer = DynSongLayer::LevelInfo;

    // Si el perfil quedo abierto por error, limpiarlo — el usuario ya esta en otro layer
    if (m_profileOpen) {
        clearProfileContext();
    }

    auto* dsm = DynamicSongManager::get();
    dsm->enterLayer(DynSongLayer::LevelInfo);

    if (playImmediately) {
        playDynamicForCurrentContext();
    }
}

void AudioContextCoordinator::deactivateLevelInfo(bool returnsToLevelSelect) {
    auto* dsm = DynamicSongManager::get();
    dsm->exitLayer(DynSongLayer::LevelInfo);

    // Dynamic songs solo suenan en LevelInfoLayer — siempre detener al salir
    if (m_dynamicContextLayer == DynSongLayer::LevelInfo) {
        m_dynamicContextLayer = DynSongLayer::None;
    }

    if (!m_profileOpen) {
        if (dsm->isActive()) {
            dsm->stopSong(); // stopSong handles menu music restoration
        } else {
            restoreMenuMusic();
        }
    }
}

void AudioContextCoordinator::beginGameplayTransition() {
    m_gameplayActive = true;
    m_dynamicContextLayer = DynSongLayer::None;
    DynamicSongManager::get()->fadeOutForLevelStart();
}

void AudioContextCoordinator::notifyGameplayStarted() {
    m_gameplayActive = true;
    m_dynamicContextLayer = DynSongLayer::None;
    m_levelInfoLevel = nullptr;
    DynamicSongManager::get()->forceKill();
}

void AudioContextCoordinator::activateProfile(int accountID) {
    ++m_profileSessionToken;
    m_profileOpen = true;
    m_profileAccountID = accountID;
    m_gameplayActive = false;

    // Save current menu music position before profile music replaces it
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* gm = GameManager::get();
    if (engine && gm) {
        m_savedMenuMusicTrack = gm->getMenuMusicFile();
        m_savedMenuMusicPosMs = engine->getMusicTimeMS(0);
    }

    suspendDynamicForProfileMusicIfNeeded();
    ProfileMusicManager::get().playProfileMusic(accountID);
}

void AudioContextCoordinator::activateProfile(int accountID, ProfileMusicManager::ProfileMusicConfig const& config) {
    ++m_profileSessionToken;
    m_profileOpen = true;
    m_profileAccountID = accountID;
    m_gameplayActive = false;

    // Save current menu music position before profile music replaces it
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* gm = GameManager::get();
    if (engine && gm) {
        m_savedMenuMusicTrack = gm->getMenuMusicFile();
        m_savedMenuMusicPosMs = engine->getMusicTimeMS(0);
    }

    suspendDynamicForProfileMusicIfNeeded();
    ProfileMusicManager::get().playProfileMusic(accountID, config);
}

void AudioContextCoordinator::updateProfileMusicConfig(int accountID, ProfileMusicManager::ProfileMusicConfig const& config) {
    // Actualizar config sin incrementar session token (evita desincronizacion por doble activacion)
    m_profileOpen = true;
    m_profileAccountID = accountID;
    suspendDynamicForProfileMusicIfNeeded();
    ProfileMusicManager::get().playProfileMusic(accountID, config);
}

void AudioContextCoordinator::clearProfileContext() {
    m_profileOpen = false;
    m_profileAccountID = 0;
}

bool AudioContextCoordinator::shouldSuspendDynamicForProfileMusic() const {
    auto* dsm = DynamicSongManager::get();
    return dsm->isActive() && !dsm->hasSuspendedPlayback();
}

void AudioContextCoordinator::suspendDynamicForProfileMusicIfNeeded() {
    if (shouldSuspendDynamicForProfileMusic()) {
        DynamicSongManager::get()->suspendPlaybackForExternalAudio();
    }
}

bool AudioContextCoordinator::restoreSuspendedDynamicSong() {
    auto* dsm = DynamicSongManager::get();
    if (!dsm->hasSuspendedPlayback()) {
        return false;
    }

    dsm->resumeSuspendedPlayback();

    // Verificar que la restauracion realmente inicio audio.
    // resumeSuspendedPlayback puede abortar por guards internos (menu music
    // desactivado, volumen 0, layer invalido) sin cargar nada.
    return dsm->isActive();
}

void AudioContextCoordinator::restoreAfterProfileMusicStop(bool hadProfileAudio, uint32_t sessionToken) {
    if (!isCurrentProfileSession(sessionToken)) {
        return;
    }

    releaseProfileLikeAudio(sessionToken);

    if (restoreSuspendedDynamicSong()) {
        return;
    }

    if (playDynamicForCurrentContext(true)) {
        return;
    }

    restoreMenuMusic();
}

void AudioContextCoordinator::handleProfileClosedAfterForceStop(bool hadProfileAudio, uint32_t sessionToken) {
    if (!isCurrentProfileSession(sessionToken)) {
        return;
    }

    clearProfileContext();
    releaseProfileLikeAudio(sessionToken);
    restoreAfterProfileMusicStop(hadProfileAudio, sessionToken);
}

bool AudioContextCoordinator::playDynamicForCurrentContext(bool ignoreProfileGate) {
    if ((m_profileOpen && !ignoreProfileGate) || m_gameplayActive) {
        return false;
    }

    if (!Mod::get()->getSettingValue<bool>("dynamic-song")) {
        return false;
    }

    auto* dsm = DynamicSongManager::get();
    switch (m_dynamicContextLayer) {
    case DynSongLayer::LevelInfo:
        if (m_levelInfoLevel) {
            dsm->enterLayer(DynSongLayer::LevelInfo);
            dsm->playSong(m_levelInfoLevel);
            return true;
        }
        return false;

    case DynSongLayer::LevelSelect:
        if (m_levelSelectLevelID > 0 && m_levelSelectLevelID <= 22) {
            auto* level = GJGameLevel::create();
            level->m_levelID = m_levelSelectLevelID;
            level->m_audioTrack = m_levelSelectLevelID - 1;
            dsm->playSong(level);
            return true;
        }
        return false;

    case DynSongLayer::None:
    default:
        return false;
    }
}

void AudioContextCoordinator::restoreMenuMusic() {
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* gm = GameManager::get();
    if (!engine || !gm) return;
    if (gm->getGameVariable("0122")) return;
    if (engine->m_musicVolume <= 0.0f) return;
    // No restaurar menu music si el audio del video esta activo
    if (paimon::isVideoAudioInteropActive()) return;

    std::string menuTrack = gm->getMenuMusicFile();
    DynamicSongManager::s_selfPlayMusic = true;
    engine->playMusic(menuTrack, true, 0.0f, 0);
    DynamicSongManager::s_selfPlayMusic = false;
    m_mainAudioOwner = MainAudioOwner::Menu;
    m_mainAudioOwnerToken = 0;

    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);

        // Resume from saved position if the track matches what was playing before
        if (m_savedMenuMusicPosMs > 0 && menuTrack == m_savedMenuMusicTrack) {
            FMOD::Channel* ch = nullptr;
            if (engine->m_backgroundMusicChannel->getChannel(0, &ch) == FMOD_OK && ch) {
                ch->setPosition(m_savedMenuMusicPosMs, FMOD_TIMEUNIT_MS);
            }
        }
    }

    m_savedMenuMusicPosMs = 0;
    m_savedMenuMusicTrack.clear();
}
