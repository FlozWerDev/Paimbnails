// MenuLoopFMODHook — tracks the playback position of the active menu
// loop channel so the popup's seek bar can display and control it.
//
// Mirrors the behaviour of the reference mod (menuloop_randomizer): on
// every FMODAudioEngine::update tick, if the currently playing track is
// the one MenuLoopManager thinks is current, we cache the channel
// position in milliseconds. The MenuMusicPopup reads it back to draw
// the progress bar and to compute skipForward/skipBackward targets.
//
// The hook also drives the "constant shuffle" auto-advance: when the
// user enabled menuLoopConstantShuffle and the current track finishes,
// we trigger a new shuffle so the next song plays automatically.

#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonMenuLoopFMODHook, FMODAudioEngine) {
    $override
    void stopAllMusic(bool p0) {
        // When GD stops music while in the menu (no level running) we
        // freeze position tracking so restoreLastMenuLoopPosition() can
        // resume from the right point.
        if (!GJBaseGameLayer::get()) {
            paimon::menuloop::MenuLoopManager::get().setPauseSongPositionTracking(true);
        }
        FMODAudioEngine::stopAllMusic(p0);
    }

    $override
    void update(float dt) {
        FMODAudioEngine::update(dt);

        auto& sm = paimon::menuloop::MenuLoopManager::get();

        // We only care about the menu music channel. If we're in gameplay
        // or the user disabled menu music, skip.
        if (GJBaseGameLayer::get() || paimon::menuloop::isVanillaMenuLoopDisabled()) return;

        auto* fmod = FMODAudioEngine::get();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        auto* channel = fmod->getActiveMusicChannel(0);
        if (!channel) return;

        const auto activeSong = fmod->getActiveMusic(0);
        const auto trackedSong = sm.getCurrentSong();
        if (activeSong != trackedSong) return;

        // Cache the raw position so seek UI can show it.
        unsigned int position = 0;
        if (channel->getPosition(&position, FMOD_TIMEUNIT_MS) == FMOD_OK) {
            if (!sm.getPauseSongPositionTracking()) {
                sm.setLastMenuLoopPosition(static_cast<int>(position));
            }
        }

        // Constant shuffle auto-advance: if we're at the end of the song
        // (within 100ms) and the user enabled constant shuffle, swap.
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
