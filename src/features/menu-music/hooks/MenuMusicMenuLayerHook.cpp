// Hook al MenuLayer de GD para:
//   1. Anadir un boton "vinyl" al right-side-menu que abre el popup principal.
//   2. Mostrar un NowPlayingToast cuando cambia el track (unica vez por
//      entrada al MenuLayer para no spamear al usuario).
//   3. Detectar cuando la cancion actual termina y reproducir la siguiente
//      automaticamente (auto-next).
//
// Se mantiene separado del hook del sistema menu-loop (que maneja override)
// para que si el usuario desactiva menuMusicEnable se puedan convivir.

#include <Geode/modify/MenuLayer.hpp>
#include "../../../framework/HookConventions.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include "../ui/MenuMusicPopup.hpp"
#include "../ui/components/NowPlayingToast.hpp"
#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../services/MenuMusicCoverLog.hpp"

using namespace geode::prelude;
using namespace paimon::menumusic;

namespace {
    // Para detectar si ya mostramos el toast al entrar al MenuLayer, lo
    // suficientemente local como para no necesitar una struct Fields.
    bool s_lastToastShown = false;
    std::string s_lastToastTrackId;
}

class $modify(PaimonMenuMusicMenuLayer, MenuLayer) {
    static void onModify(auto& self) {
        // Importante: correr DESPUES de node-ids para que right-side-menu exista.
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    $override
    bool init() {
        if (!MenuLayer::init()) return false;

        if (!Mod::get()->getSettingValue<bool>("menuMusicEnable")) return true;

        // Boton del right-side-menu.
        auto* rightMenu = typeinfo_cast<CCMenu*>(this->getChildByID("right-side-menu"));
        if (rightMenu) {
            // Evitar duplicar si ya fue inyectado por otra pasada del hook.
            if (!rightMenu->getChildByID("menu-music-btn"_spr)) {
                auto spr = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png");
                if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
                if (spr) {
                    spr->setScale(0.72f);
                    auto btn = CCMenuItemSpriteExtra::create(
                        spr, this,
                        menu_selector(PaimonMenuMusicMenuLayer::onOpenMenuMusicPopup));
                    if (btn) {
                        btn->setID("menu-music-btn"_spr);
                        rightMenu->addChild(btn);
                        rightMenu->updateLayout();
                    }
                }
            }
        }

        // Toast "Now Playing" al entrar por primera vez con un track activo.
        auto& player = MenuMusicPlayer::get();
        auto* current = player.currentTrack();
        if (current && current->id != s_lastToastTrackId) {
            s_lastToastTrackId = current->id;
            s_lastToastShown = true;
            // Diferimos un frame para que el MenuLayer termine de construirse.
            this->scheduleOnce(
                schedule_selector(PaimonMenuMusicMenuLayer::showToastNextFrame), 0.f);
        }

        // Auto-next: detectar fin de cancion y pasar a la siguiente
        // Solo si el modo no es Disabled y hay tracks en la libreria.
        auto& lib = MenuMusicLibrary::get();
        if (lib.mode() != paimon::menumusic::PlaybackMode::Disabled && lib.hasTracks()) {
            this->schedule(
                schedule_selector(PaimonMenuMusicMenuLayer::tickAutoNext), 1.0f);
        }

        return true;
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonMenuMusicMenuLayer::tickAutoNext));
        this->unschedule(schedule_selector(PaimonMenuMusicMenuLayer::showToastNextFrame));
        MenuLayer::onExit();
    }

    void onOpenMenuMusicPopup(CCObject*) {
        paimon::menumusic::coverlog::info("[MenuMusicCover] opening popup from menu button");
        if (auto popup = MenuMusicPopup::create()) {
            popup->show();
        } else {
            paimon::menumusic::coverlog::warn("[MenuMusicCover] MenuMusicPopup::create() failed");
        }
    }

    void showToastNextFrame(float) {
        NowPlayingToast::showForCurrent(this);
    }

    // Auto-next tick
    //
    // Cada segundo comprobamos si la cancion actual esta a punto de
    // terminar (ultimos 2 segundos) o ya termino. Si es asi, llamamos
    // a playNext() para que el player elija otra cancion aleatoria.
    void tickAutoNext(float) {
        auto& player = MenuMusicPlayer::get();
        auto& lib = MenuMusicLibrary::get();

        // Si el modo esta desactivado o el player esta en pausa, no hacer nada.
        if (lib.mode() == paimon::menumusic::PlaybackMode::Disabled || player.isPaused()) return;

        // Solo actuar si hay un track activo del sistema menu-music.
        if (player.state().currentTrackId.empty()) return;

        auto* fmod = FMODAudioEngine::sharedEngine();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        int numCh = 0;
        fmod->m_backgroundMusicChannel->getNumChannels(&numCh);
        if (numCh <= 0) return;

        FMOD::Channel* ch = nullptr;
        if (fmod->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK || !ch) return;

        // Comprobar si el canal sigue reproduciendo.
        bool isPlaying = false;
        ch->isPlaying(&isPlaying);
        if (!isPlaying) {
            // La cancion termino — pasar a la siguiente.
            if (player.playNext()) {
                NowPlayingToast::showForCurrent(this);
            }
            return;
        }

        unsigned int pos = 0, len = 0;
        FMOD::Sound* sound = nullptr;
        if (ch->getPosition(&pos, FMOD_TIMEUNIT_MS) != FMOD_OK) return;
        if (ch->getCurrentSound(&sound) != FMOD_OK || !sound) return;
        if (sound->getLength(&len, FMOD_TIMEUNIT_MS) != FMOD_OK) return;

        // No auto-advance near loop boundary — GD menu music is usually looped.
        FMOD_MODE mode = FMOD_DEFAULT;
        if (sound->getMode(&mode) == FMOD_OK &&
            (mode & FMOD_LOOP_NORMAL || mode & FMOD_LOOP_BIDI)) {
            return;
        }

        if (len > 0 && pos > 0 && (len - pos) <= 2000) {
            if (player.playNext()) {
                NowPlayingToast::showForCurrent(this);
            }
        }
    }
};
