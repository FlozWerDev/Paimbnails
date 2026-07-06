#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonMenuLoopFMODHook, FMODAudioEngine) {
    static void onModify(auto& self) {
        // stopAllMusic is non-virtual (hooked by address); a GD offset change would silently leave it unhooked.
        if (auto h = self.getHook("FMODAudioEngine::stopAllMusic"); !h) {
            log::warn("[MenuLoop] failed to install hook on FMODAudioEngine::stopAllMusic — "
                      "seek pause-tracking will be inactive ({})", h.unwrapErr());
        }
        if (auto h = self.getHook("FMODAudioEngine::update"); !h) {
            log::warn("[MenuLoop] failed to install hook on FMODAudioEngine::update — "
                      "seek/shuffle tracking will be inactive ({})", h.unwrapErr());
        }
    }

    $override
    void stopAllMusic(bool p0) {
        // Guard against atexit: the singleton may be shutting down.
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

        // Early-out during shutdown: touching channel/singleton state here could be UAF.
        if (paimon::isRuntimeShuttingDown()) return;

        auto& sm = paimon::menuloop::MenuLoopManager::get();

        if (GJBaseGameLayer::get() || paimon::menuloop::isVanillaMenuLoopDisabled()) return;

        auto* fmod = FMODAudioEngine::get();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        auto* channel = fmod->getActiveMusicChannel(0);
        if (!channel) return;

        const auto activeSong = fmod->getActiveMusic(0);
        const auto trackedSong = sm.getCurrentSong();
        if (activeSong != trackedSong) return;

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
            log::info("[MenuLoop] song finished — constant shuffle fires");
            paimon::menuloop::MenuLoopControl::constantShuffleModeNewSong();
        }
    }
};
