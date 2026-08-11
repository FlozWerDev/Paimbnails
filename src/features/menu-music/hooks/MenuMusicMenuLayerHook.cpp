// MenuLayer hook for the vinyl button, now-playing toast, and auto-next.
// Kept separate from menu-loop override so the features can be disabled independently.

#include <Geode/modify/MenuLayer.hpp>
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include "../ui/MenuMusicPopup.hpp"
#include "../ui/components/NowPlayingToast.hpp"
#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicEffects.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../services/MenuMusicCoverLog.hpp"

using namespace geode::prelude;
using namespace paimon::menumusic;

namespace {
    std::string s_lastToastTrackId;
    bool s_restoredSessionPlayback = false;
}

class $modify(PaimonMenuMusicMenuLayer, MenuLayer) {
    static void onModify(auto& self) {
// Run after node IDs so right-side-menu exists.
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    $override
    bool init() {
        if (!MenuLayer::init()) return false;

        if (!paimon::modules::isEnabled("paimbnails.menumusic.menu")) return true;


        auto& lib = MenuMusicLibrary::get();
        lib.load();
        lib.syncDownloadedSongs(/*force=*/false);
        auto& player = MenuMusicPlayer::get();
        if (!s_restoredSessionPlayback) {
            s_restoredSessionPlayback = true;
            const bool remember = Mod::get()->getSettingValue<bool>(
                "menuLoopSaveSongOnGameClose");
            const bool autoplay = Mod::get()->getSavedValue<bool>(
                "menuMusicAutoplayOnBoot", false);
            if (lib.mode() != paimon::menumusic::PlaybackMode::Disabled
                && (remember || autoplay)) {
                if (!remember || lib.lastTrackId().empty()
                    || !player.playSpecific(lib.lastTrackId())) {
                    player.playNext();
                }
            }
        }
        MenuMusicEffects::get().update();

        auto* rightMenu = typeinfo_cast<CCMenu*>(this->getChildByID("right-side-menu"));
        if (rightMenu) {
// Avoid duplicate injection.
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

// Show one Now Playing toast when entering with an active track.
        auto* current = player.currentTrack();
        if (current && current->id != s_lastToastTrackId) {
            s_lastToastTrackId = current->id;
// Defer one frame until MenuLayer finishes building.
            this->scheduleOnce(
                schedule_selector(PaimonMenuMusicMenuLayer::showToastNextFrame), 0.f);
        }

        this->schedule(schedule_selector(PaimonMenuMusicMenuLayer::tickAutoNext), 1.0f);

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

// Check once per second and advance in the final two seconds or after end.
    void tickAutoNext(float) {
        MenuMusicEffects::get().update();
        auto& player = MenuMusicPlayer::get();
        auto& lib = MenuMusicLibrary::get();

        if (lib.mode() == paimon::menumusic::PlaybackMode::Disabled || player.isPaused()
            || !Mod::get()->getSettingValue<bool>("menuLoopConstantShuffle")) return;

        if (player.state().currentTrackId.empty()) return;

        auto* fmod = FMODAudioEngine::sharedEngine();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        int numCh = 0;
        fmod->m_backgroundMusicChannel->getNumChannels(&numCh);
        if (numCh <= 0) return;

        FMOD::Channel* ch = nullptr;
        if (fmod->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK || !ch) return;

        bool isPlaying = false;
        ch->isPlaying(&isPlaying);
        if (!isPlaying) {
// Advance when the current song ends.
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

        if (len > 0 && pos > 0 && (len - pos) <= 2000) {
            if (player.playNext()) {
                NowPlayingToast::showForCurrent(this);
            }
        }
    }
};
