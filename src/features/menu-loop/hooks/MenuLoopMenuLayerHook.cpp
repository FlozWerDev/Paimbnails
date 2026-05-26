#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GameManager.hpp>
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

// ── Hook GameManager ─────────────────────────────────────────────
//
// Seguimos la misma estrategia que el mod de referencia Menu Loop
// Randomizer (MLR): en vez de reimplementar `playMenuMusic` (que se rompe
// facilmente en PlayerManagement, LevelSelect, etc.), solo hookeamos
// `getMenuMusicFile` para devolver el path custom. De esta manera:
//   * GD (y tambien nuestro propio PaimonLevelSelectLayer al llamar a
//     `playMusic(gm->getMenuMusicFile(), ...)`) siempre recibe la cancion
//     correcta, aunque el menu este construido por terceros o por nuestra
//     logica de restore despues de salir de un nivel.
//   * No tenemos que replicar la logica interna de GD para cargar/reanudar
//     el track del menu en el canal 0. Eso evita bugs tipo "vuelve el
//     menuLoop vanilla al salir de un nivel con dynamic song activo".
//
// `fadeInMenuMusic` tambien se hookea unicamente para restaurar la
// posicion guardada tras salir de un nivel.
class $modify(PaimonMenuLoopGameManager, GameManager) {
    $override
    gd::string getMenuMusicFile() {
        auto& sm = MenuLoopManager::get();
        const std::string& song = sm.getCurrentSong();
        if (song.empty() || song == "menuLoop.mp3") {
            return GameManager::getMenuMusicFile();
        }
        // Verificamos que el archivo existe para evitar que GD intente
        // reproducir un path roto (lo que dejaria silencio total en vez
        // del menu loop vanilla).
        std::error_code ec;
        if (!std::filesystem::is_regular_file(song, ec)) {
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

// ── Hook MenuLayer ───────────────────────────────────────────────
class $modify(PaimonMenuLoopMenuLayer, MenuLayer) {
    struct Fields {
        float m_shuffleAccum = 0.f;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("MenuLayer::init", "geode.node-ids");
    }

    $override
    bool init() {
        if (!MenuLayer::init()) return false;

        auto& sm = MenuLoopManager::get();
        auto* loader = Loader::get();

        // ── Mod compat checks ──
        if (loader->isModLoaded("omgrod.geodify"))
            sm.setGeodify(loader->getLoadedMod("omgrod.geodify")->getSettingValue<bool>("menu-loop"));
        sm.setSawbladeCustomSongsFolder(loader->isModLoaded("sawblade.custom_song_folder"));
        if (loader->isModLoaded("colon.menu_loop_start_time"))
            sm.setColonMenuLoopStartTime(loader->getLoadedMod("colon.menu_loop_start_time"));

        // ── Conflict warning ──
        if (!s_shownWarning && sm.getVibecodedVentilla() && loader->isModLoaded("joseii.ventilla")) {
            FLAlertLayer::create(
                this, "Uh oh!",
                "<c_>Another mod overriding the menu loop is active!</c>\n"
                "<cy>Please check your loaded mods.</c>",
                "I Understand", nullptr, 420.f
            )->show();
            s_shownWarning = true;
        }

        // ── Add buttons ──
        // NOTE: menu-loop buttons are disabled unless the user selects a visible button mode.
        // No llamamos updateLayout() aqui porque el MainMenuLayoutManager se encarga
        // de posicionar los botones segun el layout custom del usuario.

        // ── Constant shuffle ticker ──
        if (sm.getConstantShuffleMode()) {
            this->schedule(schedule_selector(PaimonMenuLoopMenuLayer::tickConstantShuffle), 1.0f);
        }

        return true;
    }

    // ── Button builders ──
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

    // ── Callbacks ──
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

    // ── Constant shuffle tick ──
    void tickConstantShuffle(float) {
        auto& sm = MenuLoopManager::get();
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

        // If within last 2 seconds, shuffle to next song
        if (len > 0 && pos > 0 && (len - pos) <= 2000) {
            MenuLoopControl::constantShuffleModeNewSong();
            NowPlayingCard::showForCurrentSong(this);
        }
    }
};
