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
//
// Notas sobre los hooks usados (verificadas contra GeometryDash.bro de 2.2081):
//
//   - virtual void update(float dt)        -> 4884 (VIRTUAL)
//   - void stopAllMusic(bool clear)        -> 4886 (NO virtual; hookeada por
//                                             direccion)
//
// stopAllMusic NO es virtual, asi que el hook se aplica por direccion. Si una
// futura version de GD cambia el offset, Geode no puede reapuntar via vtable y
// el hook simplemente no se aplica. Validamos esto en onModify() y logueamos
// si falla, para no quedarnos con seek tracking roto en silencio.
//
// Ademas, FMODAudioEngine::update se llama cada frame; durante el shutdown
// del juego ($on_game(Exiting)) los singletons MenuLoopManager y/o el propio
// FMODAudioEngine pueden estar en estado intermedio. Verificamos
// paimon::isRuntimeShuttingDown() temprano y devolvemos sin tocar nada.

#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonMenuLoopFMODHook, FMODAudioEngine) {
    static void onModify(auto& self) {
        // Validamos que ambos hooks se aplicaron. stopAllMusic en particular
        // es no-virtual y se hookea por direccion, asi que un cambio de offset
        // en una update de GD lo dejaria sin enganche silenciosamente.
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
        // Defensa contra atexit: el manager singleton puede estar a punto
        // de destruirse o ya haber sido marcado para shutdown.
        if (!paimon::isRuntimeShuttingDown()) {
            // When GD stops music while in the menu (no level running) we
            // freeze position tracking so restoreLastMenuLoopPosition() can
            // resume from the right point.
            if (!GJBaseGameLayer::get()) {
                paimon::menuloop::MenuLoopManager::get().setPauseSongPositionTracking(true);
            }
        }
        FMODAudioEngine::stopAllMusic(p0);
    }

    $override
    void update(float dt) {
        FMODAudioEngine::update(dt);

        // Early-out durante shutdown — MenuLoopManager y FMODAudioEngine
        // pueden estar en estado intermedio durante $on_game(Exiting), y
        // tocar m_backgroundMusicChannel / getActiveMusicChannel ahi puede
        // ser UAF si los descriptores FMOD ya se liberaron.
        if (paimon::isRuntimeShuttingDown()) return;

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
