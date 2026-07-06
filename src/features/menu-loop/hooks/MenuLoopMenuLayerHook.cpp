#include <Geode/modify/MenuLayer.hpp>
#include "../../../framework/HookConventions.hpp"
#include <Geode/modify/GameManager.hpp>
#include <chrono>
#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"
#include "../ui/NowPlayingCard.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>

using namespace geode::prelude;
using namespace paimon::menuloop;

namespace {
    bool s_shownWarning = false;

    static CCMenuItemSpriteExtra* createBtn(
        const char* frameName, CCObject* target, SEL_MenuHandler selector,
        float scale = 0.75f
    ) {
        auto spr = CCSprite::createWithSpriteFrameName(frameName);
        if (!spr) return nullptr;
        spr->setScale(scale);
        return CCMenuItemSpriteExtra::create(spr, target, selector);
    }
}

// Hook GameManager: hook getMenuMusicFile() to return the custom path rather than
// reimplementing playMenuMusic(), so GD's channel-0 load/resume logic is reused.
class $modify(PaimonMenuLoopGameManager, GameManager) {
    $override
    gd::string getMenuMusicFile() {
        auto& sm = MenuLoopManager::get();
        const std::string& song = sm.getCurrentSong();
        if (song.empty() || song == "menuLoop.mp3") {
            return GameManager::getMenuMusicFile();
        }
        // Cache existence by path with a 5s TTL to avoid blocking the main thread on every call.
        static std::string s_cachedPath;
        static bool s_cachedValid = false;
        static std::chrono::steady_clock::time_point s_cachedAt{};

        auto now = std::chrono::steady_clock::now();
        bool stale = std::chrono::duration_cast<std::chrono::seconds>(
            now - s_cachedAt).count() >= 5;
        if (s_cachedPath == song && !stale) {
            if (!s_cachedValid) return GameManager::getMenuMusicFile();
            return song;
        }

        std::error_code ec;
        bool exists = std::filesystem::is_regular_file(song, ec);
        s_cachedPath = song;
        s_cachedValid = exists;
        s_cachedAt = now;

        if (!exists) {
            return GameManager::getMenuMusicFile();
        }
        return song;
    }

    $override
    void fadeInMenuMusic() {
        GameManager::fadeInMenuMusic();
        auto& sm = MenuLoopManager::get();
        if (sm.getShouldRestoreMenuLoopPoint()) {
            sm.restoreLastMenuLoopPosition();
        }
    }
};

// Hook MenuLayer
class $modify(PaimonMenuLoopMenuLayer, MenuLayer) {
    struct Fields {
        float m_shuffleAccum = 0.f;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    $override
    bool init() {
        if (!MenuLayer::init()) return false;

        auto& sm = MenuLoopManager::get();
        auto* loader = Loader::get();

        // Mod compat checks
        if (auto* geodify = loader->getLoadedMod("omgrod.geodify")) {
            sm.setGeodify(geodify->getSettingValue<bool>("menu-loop"));
        }
        sm.setSawbladeCustomSongsFolder(loader->isModLoaded("sawblade.custom_song_folder"));
        if (auto* colonStartTime = loader->getLoadedMod("colon.menu_loop_start_time")) {
            sm.setColonMenuLoopStartTime(colonStartTime);
        }

        // Conflict warning
        if (!s_shownWarning && sm.getVibecodedVentilla() && loader->isModLoaded("joseii.ventilla")) {
            FLAlertLayer::create(
                this, "Uh oh!",
                "<c_>Another mod overriding the menu loop is active!</c>\n"
                "<cy>Please check your loaded mods.</c>",
                "I Understand", nullptr, 420.f
            )->show();
            s_shownWarning = true;
        }

        // Add buttons
        // NOTE: menu-loop buttons are disabled unless the user selects a visible button mode.
        // MainMenuLayoutManager positions them according to the user's custom layout.

        // Constant shuffle ticker
        if (sm.getConstantShuffleMode()) {
            this->schedule(schedule_selector(PaimonMenuLoopMenuLayer::tickConstantShuffle), 1.0f);
        }

        return true;
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonMenuLoopMenuLayer::tickConstantShuffle));
        MenuLayer::onExit();
    }

    // Button builders
    void addControlsButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_optionsBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onControlsButton))) {
            btn->setID("menu-loop-controls-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addShuffleButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_shuffleBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onShuffleButton))) {
            btn->setID("menu-loop-shuffle-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addRegenButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_reloadBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onRegenButton))) {
            btn->setID("menu-loop-regen-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addCopyButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_copyBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onCopyButton))) {
            btn->setID("menu-loop-copy-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addBlacklistButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_deleteBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onBlacklistButton))) {
            btn->setID("menu-loop-blacklist-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addFavoriteButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_starBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onFavoriteButton))) {
            btn->setID("menu-loop-favorite-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addHoldButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_pauseBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onHoldButton))) {
            btn->setID("menu-loop-hold-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addPreviousButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_arrow_01_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onPreviousButton))) {
            btn->setID("menu-loop-prev-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addAddButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_plusBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onAddButton))) {
            btn->setID("menu-loop-add-btn"_spr);
            menu->addChild(btn);
        }
    }
    void addSongListButton(CCMenu* menu) {
        if (auto btn = createBtn("GJ_listBtn_001.png", this, menu_selector(PaimonMenuLoopMenuLayer::onSongListButton))) {
            btn->setID("menu-loop-list-btn"_spr);
            menu->addChild(btn);
        }
    }

    // Callbacks
    void onShuffleButton(CCObject*) { MenuLoopControl::shuffleSong(); NowPlayingCard::showForCurrentSong(this); }
    void onRegenButton(CCObject*) { NowPlayingCard::showForCurrentSong(this); }
    void onCopyButton(CCObject*) { MenuLoopControl::copySong(); }
    void onBlacklistButton(CCObject*) { MenuLoopControl::blacklistSong(); NowPlayingCard::showForCurrentSong(this); }
    void onFavoriteButton(CCObject*) { MenuLoopControl::favoriteSong(); }
    void onHoldButton(CCObject*) { MenuLoopControl::holdSong(); NowPlayingCard::showForCurrentSong(this); }
    void onPreviousButton(CCObject*) { MenuLoopControl::previousSong(); NowPlayingCard::showForCurrentSong(this); }
    void onAddButton(CCObject*) {
        // TODO: open add music popup
        Notification::create("Add Music: Use mod settings config dir", NotificationIcon::Info)->show();
    }
    void onSongListButton(CCObject*) {
        // TODO: open song list popup
        Notification::create("Song list coming soon", NotificationIcon::Info)->show();
    }
    void onControlsButton(CCObject*) {
        // TODO: open control panel
        Notification::create("Control panel coming soon", NotificationIcon::Info)->show();
    }

    // Constant shuffle tick
    void tickConstantShuffle(float) {
        auto& sm = MenuLoopManager::get();
        if (sm.isOverride()) return;
        if (!sm.getConstantShuffleMode()) {
            this->unschedule(schedule_selector(PaimonMenuLoopMenuLayer::tickConstantShuffle));
            return;
        }
        if (isVanillaMenuLoopDisabled()) return;
        if (sm.songSizeIsBad()) return;

        auto* fmod = FMODAudioEngine::get();
        if (!fmod || !fmod->m_backgroundMusicChannel) return;

        int numCh = 0;
        fmod->m_backgroundMusicChannel->getNumChannels(&numCh);
        if (numCh <= 0) return;
        FMOD::Channel* ch = nullptr;
        if (fmod->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK || !ch) return;

        unsigned int pos = 0, len = 0;
        FMOD::Sound* sound = nullptr;
        if (ch->getPosition(&pos, FMOD_TIMEUNIT_MS) != FMOD_OK) return;
        if (ch->getCurrentSound(&sound) != FMOD_OK || !sound) return;
        if (sound->getLength(&len, FMOD_TIMEUNIT_MS) != FMOD_OK) return;

        FMOD_MODE mode = FMOD_DEFAULT;
        if (sound->getMode(&mode) == FMOD_OK &&
            (mode & FMOD_LOOP_NORMAL || mode & FMOD_LOOP_BIDI)) {
            return;
        }

        if (len > 0 && pos > 0 && (len - pos) <= 2000) {
            MenuLoopControl::constantShuffleModeNewSong();
            NowPlayingCard::showForCurrentSong(this);
        }
    }
};
