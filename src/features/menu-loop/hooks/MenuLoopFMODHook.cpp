#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"
#include "../../audio/services/AudioContextCoordinator.hpp"
#include "../../menu-music/services/MenuMusicPlayer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/AudioInterop.hpp"
#include "../../../utils/MusicChannel.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonMenuLoopFMODHook, FMODAudioEngine) {
    struct Fields {
        float m_songPoll = 0.f;
        std::string m_activeSong;
        std::string m_trackedSong;
    };

    static void onModify(auto& self) {
        // stopallmusic is non-virtual (hooked by address); a gd offset change would silently leave it unhooked.
        if (auto h = self.getHook("FMODAudioEngine::stopAllMusic"); !h) {
            log::warn("[MenuLoop] failed to install hook on FMODAudioEngine::stopAllMusic - "
                      "seek pause-tracking will be inactive ({})", h.unwrapErr());
        }
        if (auto h = self.getHook("FMODAudioEngine::update"); !h) {
            log::warn("[MenuLoop] failed to install hook on FMODAudioEngine::update - "
                      "seek/shuffle tracking will be inactive ({})", h.unwrapErr());
        }
    }

    $override
    void stopAllMusic(bool p0) {
        // guard against atexit: the singleton may be shutting down.
        if (!paimon::isRuntimeShuttingDown()) {
            if (!GJBaseGameLayer::get()) {
                paimon::menuloop::MenuLoopManager::get().setPauseSongPositionTracking(true);
            }
        }
        FMODAudioEngine::stopAllMusic(p0);
    }

    $override
    void update(float dt) {
        FMODAudioEngine::update(dt);

        // early-out during shutdown: touching channel/singleton state here could be uaf.
        if (paimon::isRuntimeShuttingDown()) return;

        auto& sm = paimon::menuloop::MenuLoopManager::get();

        if (GJBaseGameLayer::get() || paimon::menuloop::isVanillaMenuLoopDisabled() ||
            paimon::menumusic::MenuMusicPlayer::get().isManagingPlayback() ||
            AudioContextCoordinator::get().isGameplayActive() ||
            paimon::isProfileMusicInteropActive() ||
            paimon::isDynamicSongInteropActive() ||
            paimon::isVideoAudioInteropActive()) {
            return;
        }

        auto* fmod = FMODAudioEngine::get();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        auto* channel = paimon::audio::activeMusicChannel(fmod, 0);
        if (!channel) return;

        bool isPaused = false;
        if (channel->getPaused(&isPaused) == FMOD_OK && isPaused) return;

        m_fields->m_songPoll -= dt;
        if (m_fields->m_songPoll <= 0.f) {
            m_fields->m_songPoll = 0.25f;
            m_fields->m_activeSong = fmod->getActiveMusic(0);
            m_fields->m_trackedSong = sm.getCurrentSong();
        }
        if (m_fields->m_activeSong != m_fields->m_trackedSong) return;

        unsigned int position = 0;
        if (channel->getPosition(&position, FMOD_TIMEUNIT_MS) == FMOD_OK) {
            if (!sm.getPauseSongPositionTracking()) {
                sm.setLastMenuLoopPosition(static_cast<int>(position));
            }
        }

        if (!sm.getConstantShuffleMode() || sm.isOverride()) return;
        if (sm.isOriginalMenuLoop() || sm.getSongsSize() < 2) return;

        FMOD::Sound* sound = nullptr;
        if (channel->getCurrentSound(&sound) != FMOD_OK || !sound) return;
        bool isPlaying = true;
        channel->isPlaying(&isPlaying);
        unsigned int length = 0;
        sound->getLength(&length, FMOD_TIMEUNIT_MS);

        if (length > 100 && (length - 100) < position) {
            log::info("[MenuLoop] song finished - constant shuffle fires");
            paimon::menuloop::MenuLoopControl::constantShuffleModeNewSong();
        }
    }
};
