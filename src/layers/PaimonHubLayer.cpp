#include "PaimonHubLayer.hpp"
#include "PaiConfigLayer.hpp"
#include "PaimonSupportLayer.hpp"
#include "../features/quick-hub/ui/RadialConfigPopup.hpp"
#include "../features/paidraw/PaiDrawIcon.hpp"
#include "../features/paidraw/PaiDrawUI.hpp"
#include "../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../features/profiles/ui/ProfileSettingsPopup.hpp"
#include "../features/backgrounds/ui/BackgroundConfigPopup.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../features/cursor/ui/CursorConfigPopup.hpp"
#include "../features/pet/ui/PetConfigPopup.hpp"
#include "../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../features/progressbar/ui/ProgressBarConfigPopup.hpp"
#include "../features/custom-slider/ui/CustomSliderPopup.hpp"
#include "../features/discord-presence/ui/DiscordConfigPopup.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/beat-shaders/ui/BeatShaderConfigLayer.hpp"
#include "../features/settings-panel/services/SettingsPanelManager.hpp"
#include "../features/settings-panel/ui/SettingsCategoryBuilder.hpp"
#include "../features/settings-panel/ui/SettingsControls.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/forum/services/ForumApi.hpp"
#include "../features/forum/ui/CreatePostPopup.hpp"
#include "../features/forum/ui/PostDetailPopup.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../features/updates/ui/UpdateProgressPopup.hpp"
#include "../ui/FeatureInfoPopup.hpp"
#include "../ui/FeatureConfigPopup.hpp"
#include "../ui/HubFeatureInfo.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../utils/InfoButton.hpp"
#include "../utils/Localization.hpp"
#include "../features/guide/services/PaimonGuideService.hpp"
#include "../features/guide/GuideEvents.hpp"
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <array>
#include <Geode/binding/GameManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

namespace {
std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') {
        return fallback;
    }
    return value;
}

struct HubCategoryMeta {
    std::string title;
    std::string shortDesc;
    ccColor3B color;
    // Función que retorna las secciones de info para FeatureInfoPopup
    std::function<std::vector<paimon::ui::InfoSection>()> getInfo;
};

struct HubActionMeta {
    std::string title;
    std::string sprite;
    std::function<void(PaimonHubLayer*)> onPress;
    int categoryIndex = 0;
};

std::vector<HubCategoryMeta> getHubCategories() {
    return {
        {"General", "Idioma, updates y mantenimiento.", {130, 240, 170}, paimon::ui::getGeneralInfo},
        {"Miniaturas", "Layout, galeria, efectos y captura.", {120, 210, 255}, paimon::ui::getThumbnailsInfo},
        {"Nivel", "Pantalla de info, fondo y transiciones.", {170, 190, 255}, paimon::ui::getLevelInfoScreenInfo},
        {"Audio", "Profile music, menu music y capas.", {255, 165, 210}, paimon::ui::getAudioInfo},
        {"Fondos", "Fondos por capa, video y transiciones.", {140, 245, 200}, paimon::ui::getBackgroundsInfo},
        {"Extras", "Mascota, cursor, popups y rendimiento.", {255, 140, 140}, paimon::ui::getExtrasInfo},
        {"Discord", "Rich Presence completa.", {140, 160, 255}, paimon::ui::getDiscordInfo},
    };
}

std::vector<HubActionMeta> getHubActions(int categoryIndex) {
    switch (categoryIndex) {
        case 0: // General
            return {
                {"Configurar", "GJ_button_01.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(0); }},
                {"PaiDraw", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenPaiDraw(nullptr); }},
                {"Soporte", "GJ_button_04.png", [](PaimonHubLayer* self) { self->onOpenSupport(nullptr); }},
            };
        case 1: // Miniaturas
            return {
                {"Configurar", "GJ_button_02.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(1); }},
                {"Efectos", "GJ_button_03.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(2); }},
            };
        case 2: // Nivel
            return {
                {"Configurar", "GJ_button_01.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(3); }},
                {"Barra Progreso", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = ProgressBarConfigPopup::create()) popup->show(); }},
            };
        case 3: // Audio
            return {
                {"Configurar", "GJ_button_04.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(4); }},
                {"Musica Perfil", "GJ_button_02.png", [](PaimonHubLayer*) {
                    auto* acc = GJAccountManager::sharedState();
                    int accountID = acc ? acc->m_accountID : 0;
                    if (accountID > 0) {
                        if (auto popup = ProfileMusicPopup::create(accountID)) popup->show();
                    } else {
                        PaimonNotify::create("Necesitas iniciar sesion.", NotificationIcon::Warning)->show();
                    }
                }},
            };
        case 4: // Fondos
            return {
                {"Editor Fondos", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenBackgrounds(nullptr); }},
                {"Transiciones", "GJ_button_04.png", [](PaimonHubLayer*) { if (auto popup = TransitionConfigPopup::create()) popup->show(); }},
                {"Config Full", "GJ_button_01.png", [](PaimonHubLayer* self) { self->onOpenConfig(nullptr); }},
            };
        case 5: // Extras
            return {
                {"Mascota", "GJ_button_03.png", [](PaimonHubLayer*) { if (auto popup = PetConfigPopup::create()) popup->show(); }},
                {"Cursor", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = CursorConfigPopup::create()) popup->show(); }},
                {"Slider", "GJ_button_01.png", [](PaimonHubLayer*) { if (auto popup = paimon::slider::CustomSliderPopup::create()) popup->show(); }},
                {"Beat Shaders", "GJ_button_04.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::beat_shaders::BeatShaderConfigLayer::create()) {
                        popup->show();
                    }
                }},
                {"Perfil", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenProfiles(nullptr); }},
                {"Actualizar", "GJ_button_02.png", [](PaimonHubLayer*) {
                    auto& chk = paimon::updates::UpdateChecker::get();
                    auto state = chk.state();

                    // Si ya sabemos que hay update, ir directo al popup de descarga.
                    if (state == paimon::updates::UpdateChecker::State::UpdateAvailable) {
                        if (auto popup = paimon::updates::UpdateProgressPopup::create()) popup->show();
                        return;
                    }

                    // Si ya hay un install pendiente (auto-download lo bajo), ofrecer restart.
                    if (chk.hasPendingInstall()) {
                        geode::createQuickPopup(
                            "Actualizar",
                            fmt::format(
                                "La version <cy>{}</c> ya esta descargada.\n<cg>Reiniciar para instalar?</c>",
                                chk.remoteVersion()
                            ),
                            "No", "Reiniciar",
                            [](auto*, bool yes) {
                                if (!yes) return;
                                auto& c = paimon::updates::UpdateChecker::get();
                                if (c.restartToApplyPendingUpdate()) {
                                    geode::utils::game::exit(true);
                                } else {
                                    geode::utils::game::restart(true);
                                }
                            }
                        );
                        return;
                    }

                    // Si ya estamos al dia, notificar.
                    if (state == paimon::updates::UpdateChecker::State::UpToDate) {
                        PaimonNotify::create(
                            fmt::format("Ya tienes la ultima version ({})", chk.localVersion()),
                            NotificationIcon::Success
                        )->show();
                        return;
                    }

                    // Si esta comprobando actualmente, avisar.
                    if (state == paimon::updates::UpdateChecker::State::Checking) {
                        PaimonNotify::create("Comprobando actualizaciones...", NotificationIcon::Loading)->show();
                        return;
                    }

                    // Idle o Failed: relanzar el check y pedir al usuario que
                    // vuelva a pulsar en unos segundos.
                    chk.checkAsync();
                    PaimonNotify::create("Buscando actualizaciones... pulsa de nuevo en unos segundos.", NotificationIcon::Loading)->show();
                }},
            };
        case 6: // Discord
            return {
                {"Configurar", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show(); }},
                {"Refrescar", "GJ_button_05.png", [](PaimonHubLayer*) { paimon::discord::DiscordPresenceManager::get().refreshSoon(); PaimonNotify::create("Rich Presence actualizada.", NotificationIcon::Success)->show(); }},
            };
        default:
            return {};
    }
}

struct GranularSettingMeta {
    std::string englishName;
    std::string spanishName;
    int categoryIndex;
};

std::vector<GranularSettingMeta> getGranularSettings() {
    return {
        // Category 0: General
        {"Language / Idioma", "Idioma / Language", 0},
        {"Auto Update", "Auto Actualizar", 0},
        {"Quick Search Key", "Tecla de Búsqueda Rápida", 0},
        {"Realtime Search Preview", "Vista Previa en Tiempo Real", 0},
        {"Settings Panel Keybind", "Tecla de Panel de Ajustes", 0},
        {"Layout Editor Keybind", "Tecla de Editor de Layout", 0},
        {"Debug Logs", "Registros de Depuración", 0},

        // Category 1: Miniaturas / Thumbnails
        {"Thumbnail Size", "Tamaño de Miniatura", 1},
        {"Background Style (Cell)", "Estilo de Fondo de Celda", 1},
        {"Background Blur (Cell)", "Desenfoque de Fondo de Celda", 1},
        {"Darkness (Cell)", "Oscuridad de Celda", 1},
        {"Show Separator", "Mostrar Separador", 1},
        {"Show View Button", "Mostrar Botón de Ver", 1},
        {"Compact Mode", "Modo Compacto", 1},
        {"Show Compact Toggle", "Mostrar Botón de Modo Compacto", 1},
        {"Auto-Cycle Gallery", "Auto-Ciclo de Galería", 1},
        {"Transition Type", "Tipo de Transición", 1},
        {"Transition Duration", "Duración de Transición", 1},
        {"Hover Effects", "Efectos al pasar el mouse", 1},
        {"Animation Type", "Tipo de Animación", 1},
        {"Animation Speed", "Velocidad de Animación", 1},
        {"Color Effect", "Efecto de Color", 1},
        {"Effect on Background", "Efecto en Fondo", 1},
        {"Enable Capture Button", "Activar Botón de Captura", 1},
        {"Capture Thumbnail Key", "Tecla de Capturar Miniatura", 1},

        // Category 2: Nivel / Level Info
        {"Background Style (Level)", "Estilo de Fondo de Nivel", 2},
        {"Dynamic Song", "Canción Dinámica", 2},
        {"Progress Bar", "Barra de Progreso", 2},

        // Category 3: Audio
        {"Enable Profile Music", "Activar Música de Perfil", 3},
        {"Enable Menu Music Player", "Activar Reproductor de Música de Menú", 3},
        {"Menu Loop Shuffle", "Mezclar Bucles de Menú", 3},
        {"Now Playing Notifications", "Notificaciones de Reproducción", 3},
        {"Notification Duration", "Duración de la Notificación", 3},
        {"Notification Prefix", "Prefijo de Notificación", 3},
        {"Seek Step (ms)", "Paso de Búsqueda (ms)", 3},
        {"Show Playback Progress", "Mostrar Progreso de Reproducción", 3},
        {"Menu Music Hotkeys", "Teclas Rápidas de Música", 3},
        {"Remember Last Menu Loop", "Recordar Último Bucle de Menú", 3},
        {"Randomize on Level Exit", "Aleatorio al Salir del Nivel", 3},
        {"Restore Position on Level Exit", "Restaurar Posición al Salir de Nivel", 3},
        {"Randomize on Editor Exit", "Aleatorio al Salir del Editor", 3},
        {"Restore Position on Editor Exit", "Restaurar Posición al Salir del Editor", 3},

        // Category 4: Fondos / Backgrounds
        {"Editor Fondos", "Editor de Fondos", 4},
        {"Transiciones de Fondos", "Transiciones de Fondos", 4},
        {"Configuración Completa", "Configuración Completa", 4},

        // Category 5: Extras
        {"Enable Pet", "Activar Mascota", 5},
        {"Pet Sprite / Pet Type", "Sprite de Mascota / Tipo de Mascota", 5},
        {"Pet Scale", "Escala de Mascota", 5},
        {"Pet Opacity", "Opacidad de Mascota", 5},
        {"Custom Cursor", "Cursor Personalizado", 5},
        {"Cursor Trail", "Estela de Cursor", 5},
        {"Cursor Scale", "Escala de Cursor", 5},
        {"Score Cell Style", "Estilo de Celda de Puntuación", 5},
        {"Custom Slider Thumb", "Barra de Desplazamiento Personalizada", 5},
        {"Dynamic Popups", "Popups Dinámicos", 5},
        {"Dynamic Popup Exit", "Salida de Popup Dinámica", 5},
        {"Popup Blur", "Desenfoque de Popup", 5},
        {"Download Threads", "Hilos de Descarga / Hilos de Red", 5},
        {"Disk Cache", "Caché de Disco", 5},
        {"Clear Cache on Exit", "Limpiar Caché al Salir", 5},
        {"Open Thumbnails Folder", "Abrir Carpeta de Miniaturas", 5},

        // Category 6: Discord
        {"Enable Discord Rich Presence", "Activar Discord Rich Presence", 6},
        {"Configure Discord RPC", "Configurar Discord RPC", 6},
        {"Refresh Discord Status", "Refrescar Estado de Discord", 6}
    };
}
}

PaimonHubLayer* PaimonHubLayer::create() {
    auto ret = new PaimonHubLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaimonHubLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(PaimonHubLayer::create());
    return scene;
}

PaimonHubLayer::~PaimonHubLayer() {
    if (m_createPostOverlay && m_createPostOverlay->getParent()) {
        m_createPostOverlay->removeFromParent();
    }
    if (m_createTagOverlay && m_createTagOverlay->getParent()) {
        m_createTagOverlay->removeFromParent();
    }
    if (m_predefPickerOverlay && m_predefPickerOverlay->getParent()) {
        m_predefPickerOverlay->removeFromParent();
    }
}

bool PaimonHubLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2;
    float top = winSize.height;

    auto bg = CCLayerColor::create(ccc4(20, 20, 32, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -2);

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("paimon-hub-main-menu"_spr);
    m_mainMenu->setPosition({0, 0});
    this->addChild(m_mainMenu, 10);

    auto title = CCLabelBMFont::create(tr("pai.hub.title", "Paimbnails").c_str(), "goldFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({46.f, top - 20.f});
    title->setScale(0.48f);
    this->addChild(title);

    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaimonHubLayer::onBack));
    backBtn->setPosition({20.f, top - 20.f});
    backBtn->setScale(0.75f);
    m_mainMenu->addChild(backBtn);

    auto helpSpr = ButtonSprite::create("?", "goldFont.fnt", "GJ_button_04.png", .72f);
    helpSpr->setScale(0.40f);
    auto helpBtn = CCMenuItemSpriteExtra::create(helpSpr, this, menu_selector(PaimonHubLayer::onOpenHelp));
    helpBtn->setPosition({winSize.width - 20.f, top - 20.f});
    m_mainMenu->addChild(helpBtn);

    float tabY = top - 20.f;
    std::vector<std::string> tabNames = {
        tr("pai.hub.tab.home", "Home"),
        tr("pai.hub.tab.news", "News"),
        tr("pai.hub.tab.forum", "Forum")
    };

    auto tabBar = CCMenu::create();
    tabBar->setID("paimon-hub-tab-bar"_spr);
    tabBar->setPosition({cx + 40.f, tabY});
    tabBar->setContentSize({200.f, 24.f});
    tabBar->setAnchorPoint({0.5f, 0.5f});
    tabBar->setLayout(
        RowLayout::create()
            ->setGap(6.f)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    this->addChild(tabBar, 10);

    for (int i = 0; i < 3; i++) {
        auto spr = ButtonSprite::create(tabNames[i].c_str(), "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(0.38f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonHubLayer::onTabSwitch));
        btn->setTag(i);
        // ID estable para identificacion entre mods (texture-loader, etc).
        // Usa _spr para que se anteponga el prefijo "flozwer.paimbnails2/".
        if (i == 0)      btn->setID("home-tab-btn"_spr);
        else if (i == 1) btn->setID("news-tab-btn"_spr);
        else             btn->setID("forum-tab-btn"_spr);
        tabBar->addChild(btn);
        m_tabBtns.push_back(btn);
    }
    tabBar->updateLayout();

    auto sep = CCLayerColor::create({100, 150, 255, 60});
    sep->setContentSize({winSize.width - 30, 1});
    sep->setPosition({15, top - 38.f});
    this->addChild(sep, 5);

    m_homeTab = cocos2d::CCLayerRGBA::create();
    m_homeTab->setID("home-tab"_spr);
    m_homeTab->setCascadeOpacityEnabled(true);
    this->addChild(m_homeTab, 5);
    m_homeMenu = CCMenu::create();
    m_homeMenu->setID("home-menu"_spr);
    m_homeMenu->setPosition({0, 0});
    this->addChild(m_homeMenu, 11);

    m_newsTab = cocos2d::CCLayerRGBA::create();
    m_newsTab->setID("news-tab"_spr);
    m_newsTab->setVisible(false);
    m_newsTab->setCascadeOpacityEnabled(true);
    this->addChild(m_newsTab, 5);
    m_newsMenu = CCMenu::create();
    m_newsMenu->setID("news-menu"_spr);
    m_newsMenu->setPosition({0, 0});
    m_newsMenu->setVisible(false);
    this->addChild(m_newsMenu, 11);

    m_forumTab = cocos2d::CCLayerRGBA::create();
    m_forumTab->setID("forum-tab"_spr);
    m_forumTab->setVisible(false);
    m_forumTab->setCascadeOpacityEnabled(true);
    this->addChild(m_forumTab, 5);
    m_forumMenu = CCMenu::create();
    m_forumMenu->setID("forum-menu"_spr);
    m_forumMenu->setPosition({0, 0});
    m_forumMenu->setVisible(false);
    this->addChild(m_forumMenu, 11);

    m_forumTags = {
        "Guide", "Tip", "Question", "Bug", "Suggestion",
        "Showcase", "Discussion", "Help", "News", "Update",
        "Level", "Video", "Art", "Music", "Story",
        "Theory", "Challenge", "Competition", "Feedback", "Other"
    };

    buildHomeTab();
    buildNewsTab();
    buildForumTab();

    switchTab(0);

    return true;
}

void PaimonHubLayer::keyBackClicked() {
    // MenuLayer::scene(false) hace el setup completo (musica, fondos, hooks).
    // Antes usabamos CCScene::create() + MenuLayer::create() manualmente,
    // lo que dejaba la escena sin inicializar => pantalla negra al apretar Escape.
    CCDirector::get()->replaceScene(MenuLayer::scene(false));
}

void PaimonHubLayer::onTabSwitch(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx >= 100 && idx <= 106) {
        switchHomeCategory(idx - 100);
        return;
    }
    switchTab(idx);
}

void PaimonHubLayer::switchTab(int idx) {
    m_currentTab = idx;

    m_homeTab->setVisible(idx == 0);
    m_homeMenu->setVisible(idx == 0);
    m_newsTab->setVisible(idx == 1);
    m_newsMenu->setVisible(idx == 1);
    m_forumTab->setVisible(idx == 2);
    m_forumMenu->setVisible(idx == 2);

    cocos2d::CCLayerRGBA* activeTab = nullptr;
    if (idx == 0)      activeTab = m_homeTab;
    else if (idx == 1) activeTab = m_newsTab;
    else if (idx == 2) activeTab = m_forumTab;

    if (activeTab) {
        activeTab->stopAllActions();
        activeTab->setOpacity(0);
        activeTab->runAction(CCFadeTo::create(0.18f, 255));
    }

    for (int i = 0; i < (int)m_tabBtns.size(); i++) {
        auto spr = typeinfo_cast<ButtonSprite*>(m_tabBtns[i]->getNormalImage());
        if (!spr) continue;
        spr->setColor(i == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
    }

    if (idx == 2) {
        refreshForumPosts();
    }
}

void PaimonHubLayer::buildHomeTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;

    auto categories = getHubCategories();

    // 1. Create Left Sidebar Panel — NineSlice canónico (square02b_001.png
    // tintado oscuro), mismo lenguaje que `Geode::Popup` y `MDTextArea`.
    // Antes usaba un CCDrawNode con borde azul-gris custom; lo dejamos
    // pasar por createDarkPanel que hace la cadena NineSlice → Scale9 →
    // CCDrawNode.
    m_sidebarBg = paimon::SpriteHelper::createDarkPanel(135.f, 250.f, 220, 6.f);
    m_sidebarBg->setPosition({15.f, 15.f});
    m_homeTab->addChild(m_sidebarBg, 0);

    // 2. Create Dynamic sliding category highlight inside the Sidebar
    // Tinte blanco para que `setColor(category.color)` aplique limpio
    // sobre la base de NineSlice (el tinte se multiplica con la textura
    // gris de `square02b_001.png`).
    m_sidebarHighlight = paimon::SpriteHelper::createColorPanel(
        125.f, 26.f, cocos2d::ccColor3B{255, 255, 255}, 200, 4.f
    );
    m_sidebarHighlight->setPosition({20.f, 241.f - 13.f});
    m_homeTab->addChild(m_sidebarHighlight, 1);
    if (m_sidebarHighlight && !categories.empty()) {
        m_sidebarHighlight->setColor(categories[0].color);
    }

    // 3. Create Sidebar Menu containing the 7 category items
    m_sidebarMenu = CCMenu::create();
    m_sidebarMenu->setPosition({0, 0});
    m_sidebarMenu->setID("paimon-sidebar-menu"_spr);
    m_homeTab->addChild(m_sidebarMenu, 2);

    m_sidebarLabels.clear();

    for (size_t i = 0; i < categories.size(); i++) {
        float yPos = 241.f - i * 27.f;

        // Custom list item label
        auto label = CCLabelBMFont::create(categories[i].title.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.5f, 0.5f});
        label->setPosition({20.f + 125.f / 2.f, yPos});
        label->setScale(0.28f);
        m_homeTab->addChild(label, 3);
        m_sidebarLabels.push_back(label);

        // Invisible CCNode for touch interaction
        auto btnBg = CCNode::create();
        btnBg->setContentSize({125.f, 26.f});

        auto btn = CCMenuItemSpriteExtra::create(btnBg, this, menu_selector(PaimonHubLayer::onTabSwitch));
        btn->setTag(100 + static_cast<int>(i));
        btn->setPosition({20.f + 125.f / 2.f, yPos});
        m_sidebarMenu->addChild(btn);
    }

    // 4. Compact Quick Shortcuts bar at the bottom of left sidebar
    //
    // Refactor (Paso 5 — feature Guia): los 3 botones manuales pasan a un
    // CCMenu con RowLayout para que un cuarto boton ("GUI" Guia) entre sin
    // descolocar nada. Mantengo ID "discord-sidebar-btn", "quickhub-sidebar-btn"
    // y "paidraw-sidebar-btn" para no romper compat con otros mods que los
    // localicen.
    {
        auto* shortcutsRow = CCMenu::create();
        shortcutsRow->setID("hub-shortcuts-row"_spr);
        shortcutsRow->setContentSize({135.f, 30.f});
        shortcutsRow->setAnchorPoint({0.5f, 0.5f});
        shortcutsRow->ignoreAnchorPointForPosition(false);
        // Posicion equivalente al centro de la fila previa (y=32 antes con
        // botones de 26px → centro vertical en y=32). +10 px en X
        // respecto al original para alinearlo con el sidebar superior.
        shortcutsRow->setPosition({135.f * 0.5f + 5.f + 10.f, 32.f});
        shortcutsRow->setLayout(
            RowLayout::create()
                ->setGap(6.f)
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );

        // ── Discord ────────────────────────────────────────────────────
        auto* discordBgBtn = CCScale9Sprite::create("GJ_button_02.png");
        discordBgBtn->setContentSize({26.f, 26.f});
        discordBgBtn->setColor({110, 150, 255});
        auto* discordLabel = CCLabelBMFont::create("RPC", "goldFont.fnt");
        discordLabel->setScale(0.26f);
        discordLabel->setPosition(discordBgBtn->getContentSize() / 2.f);
        discordBgBtn->addChild(discordLabel);

        auto* discordBtn = CCMenuItemExt::createSpriteExtra(discordBgBtn, [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
            if (auto layer = self.lock()) layer->onOpenDiscordConfig(nullptr);
        });
        discordBtn->setID("discord-sidebar-btn"_spr);
        shortcutsRow->addChild(discordBtn);

        // ── Quick Hub ──────────────────────────────────────────────────
        auto* qhBgBtn = CCScale9Sprite::create("GJ_button_02.png");
        qhBgBtn->setContentSize({26.f, 26.f});
        qhBgBtn->setColor({255, 200, 80});
        auto* qhLabel = CCLabelBMFont::create("QH", "goldFont.fnt");
        qhLabel->setScale(0.26f);
        qhLabel->setPosition(qhBgBtn->getContentSize() / 2.f);
        qhBgBtn->addChild(qhLabel);

        auto* qhBtn = CCMenuItemExt::createSpriteExtra(qhBgBtn, [](CCMenuItemSpriteExtra*) {
            if (auto popup = paimon::quickhub::RadialConfigPopup::create()) popup->show();
        });
        qhBtn->setID("quickhub-sidebar-btn"_spr);
        shortcutsRow->addChild(qhBtn);

        // ── PaiDraw ────────────────────────────────────────────────────
        auto* drawBgBtn = CCScale9Sprite::create("GJ_button_02.png");
        drawBgBtn->setContentSize({26.f, 26.f});
        if (auto* icon = paidraw::createPaiDrawIcon(20.f)) {
            icon->setPosition(drawBgBtn->getContentSize() / 2.f);
            drawBgBtn->addChild(icon);
        }

        auto* drawBtn = CCMenuItemSpriteExtra::create(drawBgBtn, this, menu_selector(PaimonHubLayer::onOpenPaiDraw));
        drawBtn->setID("paidraw-sidebar-btn"_spr);
        shortcutsRow->addChild(drawBtn);

        // ── Guia (toggle) ──────────────────────────────────────────────
        // Estado actual del toggle persistido en saved value.
        bool guideEnabled = paimon::guide::PaimonGuideService::get().isEnabled();

        auto* guideBgBtn = CCScale9Sprite::create("GJ_button_02.png");
        guideBgBtn->setContentSize({26.f, 26.f});
        guideBgBtn->setColor(guideEnabled
            ? ccColor3B{255, 220, 100}
            : ccColor3B{120, 120, 130});
        auto* guideLabel = CCLabelBMFont::create("GUI", "goldFont.fnt");
        guideLabel->setScale(0.26f);
        guideLabel->setColor(guideEnabled
            ? ccColor3B{255, 255, 255}
            : ccColor3B{180, 180, 180});
        guideLabel->setPosition(guideBgBtn->getContentSize() / 2.f);
        guideLabel->setID("guide-sidebar-btn-label"_spr);
        guideBgBtn->addChild(guideLabel);

        auto* guideBtn = CCMenuItemExt::createSpriteExtra(
            guideBgBtn,
            [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra* sender) {
                using namespace paimon::guide;
                bool current = PaimonGuideService::get().isEnabled();
                bool newState = !current;
                PaimonGuideService::get().setEnabled(newState);

                // Notificar a otros sistemas (MenuLayer hook) sin recargar
                // la escena. Paso 6 anade el listener; aqui solo emitimos.
                GuideEnabledChangedEvent(kGuideEventFilter).send(newState);

                // Feedback visual inmediato sobre el propio boton
                if (auto* spr = sender ? sender->getNormalImage() : nullptr) {
                    if (auto* nine = static_cast<cocos2d::extension::CCScale9Sprite*>(spr)) {
                        nine->setColor(newState
                            ? ccColor3B{255, 220, 100}
                            : ccColor3B{120, 120, 130});
                        if (auto* lbl = nine->getChildByIDRecursive("guide-sidebar-btn-label"_spr)) {
                            static_cast<CCLabelBMFont*>(lbl)->setColor(newState
                                ? ccColor3B{255, 255, 255}
                                : ccColor3B{180, 180, 180});
                        }
                    }
                }

                // Toast traducido
                Notification::create(
                    newState
                        ? Localization::get().getString("pai.guide.toggle.on")
                        : Localization::get().getString("pai.guide.toggle.off"),
                    newState ? NotificationIcon::Success : NotificationIcon::None,
                    1.8f
                )->show();

                (void)self; // futuro: refrescar UI del propio Hub si hace falta
            }
        );
        guideBtn->setID("guide-sidebar-btn"_spr);
        shortcutsRow->addChild(guideBtn);

        shortcutsRow->updateLayout();
        m_sidebarMenu->addChild(shortcutsRow);
    }

    // 5. Create Right Details Panel — NineSlice oscuro canónico Geode.
    // Antes tenía un borde azul brillante custom; ahora usa el borde
    // natural del 9-slice de GD para coincidir con el resto del mod.
    float detailsW = winSize.width - 175.f;
    m_detailsBg = paimon::SpriteHelper::createDarkPanel(detailsW, 250.f, 240, 6.f);
    m_detailsBg->setPosition({160.f, 15.f});
    m_homeTab->addChild(m_detailsBg, 0);

    // 6. Header Section (Title + Description + Info icon) inside Details Panel
    m_homeCategoryTitle = CCLabelBMFont::create(categories[0].title.c_str(), "goldFont.fnt");
    m_homeCategoryTitle->setScale(0.48f);
    m_homeCategoryTitle->setColor(categories[0].color);
    m_homeCategoryTitle->setAnchorPoint({0.f, 0.5f});
    m_homeCategoryTitle->setPosition({178.f, 242.f});
    m_homeTab->addChild(m_homeCategoryTitle, 2);

    m_homeCategoryInfoBtn = CCMenuItemExt::createSpriteExtra(
        CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"),
        [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
            auto layer = self.lock();
            if (!layer) return;
            auto categories = getHubCategories();
            if (layer->m_homeSelectedCategory < 0 || layer->m_homeSelectedCategory >= static_cast<int>(categories.size())) return;
            auto const& cat = categories[layer->m_homeSelectedCategory];
            auto sections = cat.getInfo();
            if (auto popup = paimon::ui::FeatureInfoPopup::create(cat.title, sections)) {
                popup->show();
            }
        }
    );
    m_homeCategoryInfoBtn->setScale(0.38f);
    m_sidebarMenu->addChild(m_homeCategoryInfoBtn);

    m_homeCategoryDesc = CCLabelBMFont::create(categories[0].shortDesc.c_str(), "bigFont.fnt");
    m_homeCategoryDesc->setScale(0.20f);
    m_homeCategoryDesc->setColor({160, 170, 190});
    m_homeCategoryDesc->setAnchorPoint({0.f, 0.5f});
    m_homeCategoryDesc->setPosition({178.f, 222.f});
    m_homeTab->addChild(m_homeCategoryDesc, 2);

    // 6b. Search Input bar in Details Header (compact text input for dynamic feature filtering)
    m_searchInput = TextInput::create(120.f, tr("pai.hub.search", "Search...").c_str(), "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setMaxCharCount(20);
    m_searchInput->setPosition({winSize.width - 95.f, 236.f});
    m_searchInput->setScale(0.68f);
    m_searchInput->setCallback([this](std::string const& text) {
        rebuildHomeCategoryCards();
    });
    m_homeTab->addChild(m_searchInput, 10);

    // 7. Action cards setup
    m_homeActionsAnchor = CCNode::create();
    m_homeTab->addChild(m_homeActionsAnchor, 2);

    m_homeActionsMenu = CCMenu::create();
    m_homeActionsMenu->setPosition({0.f, 0.f});
    m_homeMenu->addChild(m_homeActionsMenu, 3);

    // 8. Version Label
    {
        auto& chk = paimon::updates::UpdateChecker::get();
        std::string verText = fmt::format(
            fmt::runtime(tr("pai.hub.version", "Version: {}")),
            chk.localVersion()
        );
        auto* versionLabel = CCLabelBMFont::create(verText.c_str(), "bigFont.fnt");
        versionLabel->setScale(0.24f);
        versionLabel->setColor({120, 130, 150});
        versionLabel->setPosition({15.f + 135.f / 2.f, 13.f});
        m_homeTab->addChild(versionLabel, 2);
    }

    switchHomeCategory(0);
}

void PaimonHubLayer::switchHomeCategory(int idx) {
    auto categories = getHubCategories();
    if (idx < 0 || idx >= static_cast<int>(categories.size())) return;
    m_homeSelectedCategory = idx;

    if (m_searchInput) {
        m_searchInput->setString("");
    }

    if (m_homeActionsScroll) {
        m_homeActionsScroll->removeFromParent();
        m_homeActionsScroll = nullptr;
    }
    if (m_homeActionsMenu) {
        m_homeActionsMenu->setVisible(true);
    }

    refreshHomeCategorySelector();

    if (m_homeCategoryTitle) {
        m_homeCategoryTitle->setString(categories[idx].title.c_str());
        m_homeCategoryTitle->setColor(categories[idx].color);
    }
    if (m_homeCategoryDesc) {
        m_homeCategoryDesc->setString(categories[idx].shortDesc.c_str());
    }
    rebuildHomeCategoryCards();
}

void PaimonHubLayer::refreshHomeCategorySelector() {
    auto categories = getHubCategories();
    if (m_homeSelectedCategory < 0 || m_homeSelectedCategory >= static_cast<int>(categories.size())) return;

    // Smoothly slide the category highlight to the new position using EaseExponentialOut
    float targetY = 241.f - m_homeSelectedCategory * 27.f;
    if (m_sidebarHighlight) {
        m_sidebarHighlight->stopAllActions();
        m_sidebarHighlight->runAction(CCEaseExponentialOut::create(
            CCMoveTo::create(0.22f, {20.f, targetY - 13.f})
        ));
        m_sidebarHighlight->runAction(CCTintTo::create(0.22f, categories[m_homeSelectedCategory].color.r, categories[m_homeSelectedCategory].color.g, categories[m_homeSelectedCategory].color.b));
    }

    // Refresh label scaling/colors
    for (size_t i = 0; i < m_sidebarLabels.size(); i++) {
        if (!m_sidebarLabels[i]) continue;
        if (static_cast<int>(i) == m_homeSelectedCategory) {
            m_sidebarLabels[i]->setColor({255, 255, 255});
            m_sidebarLabels[i]->runAction(CCScaleTo::create(0.12f, 0.32f));
        } else {
            m_sidebarLabels[i]->setColor({130, 140, 165});
            m_sidebarLabels[i]->runAction(CCScaleTo::create(0.12f, 0.28f));
        }
    }

    // Position Info Button dynamically to the right of the Title text
    if (m_homeCategoryInfoBtn && m_homeCategoryTitle) {
        float titleW = m_homeCategoryTitle->getScaledContentSize().width;
        m_homeCategoryInfoBtn->setPosition({178.f + titleW + 12.f, 242.f});
    }
}

void PaimonHubLayer::onPrevHomeCategory(CCObject*) {}
void PaimonHubLayer::onNextHomeCategory(CCObject*) {}

void PaimonHubLayer::onOpenHelp(CCObject*) {
    std::string layoutKeybind = "Ctrl+Q";
    if (auto* mod = Mod::get(); mod && mod->hasSetting("main-menu-layout-keybind")) {
        if (auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(mod->getSetting("main-menu-layout-keybind"))) {
            auto value = setting->getValue();
            if (!value.empty()) {
                layoutKeybind = value.front().toString();
            }
        }
    }

    auto body = fmt::format(
        "<cg>Atajos y controles</c>\n\n"
        "<cy>{}</c> abre o guarda el editor del layout en el menu principal y en el menu de pausa.\n"
        "Ese atajo es configurable en Geode y solo funciona en esas dos pantallas.\n\n"
        "Dentro del editor de layout:\n"
        "- Arrastra un boton para moverlo\n"
        "- Grip verde = tamano\n"
        "- Azul +/- = transparencia\n"
        "- Roja X = ocultar o mostrar\n"
        "- <cy>Esc</c> cancela\n\n"
        "En este Hub:\n"
        "- Pulsa las categorias en la barra lateral para cambiar\n"
        "- Usa los accesos directos inferiores para acciones rapidas",
        layoutKeybind
    );

    FLAlertLayer::create("Ayuda", body.c_str(), "OK")->show();
}

void PaimonHubLayer::rebuildHomeCategoryCards() {
    if (!m_homeActionsMenu) return;
    m_homeActionsMenu->removeAllChildren();

    // Remove any existing search scroll layer safely
    if (m_homeActionsScroll) {
        m_homeActionsScroll->removeFromParent();
        m_homeActionsScroll = nullptr;
    }
    m_homeActionsMenu->setVisible(true);

    auto categories = getHubCategories();
    if (m_homeSelectedCategory < 0 || m_homeSelectedCategory >= static_cast<int>(categories.size())) return;
    auto const& cat = categories[m_homeSelectedCategory];

    std::vector<HubActionMeta> actions;
    std::string query = "";
    if (m_searchInput) {
        query = m_searchInput->getString();
    }

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };

    if (query.empty()) {
        actions = getHubActions(m_homeSelectedCategory);
        for (auto& act : actions) {
            act.categoryIndex = m_homeSelectedCategory;
        }
        if (m_homeCategoryTitle) {
            m_homeCategoryTitle->setString(cat.title.c_str());
            m_homeCategoryTitle->setColor(cat.color);
        }
        if (m_homeCategoryDesc) {
            m_homeCategoryDesc->setString(cat.shortDesc.c_str());
        }
        if (m_homeCategoryInfoBtn) {
            m_homeCategoryInfoBtn->setVisible(true);
            float titleW = m_homeCategoryTitle->getScaledContentSize().width;
            m_homeCategoryInfoBtn->setPosition({178.f + titleW + 12.f, 242.f});
        }
    } else {
        std::string lowerQuery = toLower(query);
        
        // 1. Search in category-level quick actions
        for (size_t catIdx = 0; catIdx < categories.size(); ++catIdx) {
            auto catActions = getHubActions(static_cast<int>(catIdx));
            for (auto const& act : catActions) {
                if (toLower(act.title).find(lowerQuery) != std::string::npos ||
                    toLower(categories[catIdx].title).find(lowerQuery) != std::string::npos) {
                    HubActionMeta searchAct = act;
                    searchAct.title = categories[catIdx].title + ": " + act.title;
                    searchAct.categoryIndex = static_cast<int>(catIdx);
                    actions.push_back(searchAct);
                }
            }
        }

        // 2. Search in individual granular settings
        std::string lang = geode::Mod::get()->getSettingValue<std::string>("language");
        bool isSpanish = (lang == "spanish");

        auto granularSettings = getGranularSettings();
        for (auto const& gs : granularSettings) {
            std::string matchName = isSpanish ? gs.spanishName : gs.englishName;
            if (toLower(gs.englishName).find(lowerQuery) != std::string::npos ||
                toLower(gs.spanishName).find(lowerQuery) != std::string::npos) {
                
                HubActionMeta searchAct;
                searchAct.title = matchName;
                searchAct.sprite = "GJ_button_02.png";
                searchAct.categoryIndex = gs.categoryIndex;
                searchAct.onPress = [gs](PaimonHubLayer*) {
                    paimon::ui::openFeatureConfigFor(gs.englishName, gs.categoryIndex);
                };
                actions.push_back(searchAct);
            }
        }

        if (m_homeCategoryTitle) {
            m_homeCategoryTitle->setString(tr("pai.hub.search_results", "Search Results").c_str());
            m_homeCategoryTitle->setColor({255, 255, 255});
        }
        if (m_homeCategoryDesc) {
            m_homeCategoryDesc->setString(fmt::format(fmt::runtime(tr("pai.hub.search_found", "Found {} features matching search.")), actions.size()).c_str());
        }
        if (m_homeCategoryInfoBtn) {
            m_homeCategoryInfoBtn->setVisible(false);
        }
    }

    auto winSize = CCDirector::get()->getWinSize();
    float detailsW = winSize.width - 175.f;
    float rightPanelCX = 160.f + detailsW / 2.f;

    size_t n = actions.size();
    if (n == 0) return;

    if (n > 4) {
        // Hide default static menu and create a beautifully scrollable grid
        m_homeActionsMenu->setVisible(false);

        float scrollW = detailsW - 20.f;
        float scrollH = 175.f;
        
        m_homeActionsScroll = geode::ScrollLayer::create(CCSize{scrollW, scrollH});
        m_homeActionsScroll->setPosition({160.f + 10.f, 20.f});
        m_homeActionsScroll->setID("search-actions-scroll"_spr);
        m_homeTab->addChild(m_homeActionsScroll, 3);

        float cardW = 112.f;
        float cardH = 48.f;
        float colWidth = 120.f;
        float rowHeight = 56.f;

        int numCols = static_cast<int>(scrollW / colWidth);
        if (numCols < 1) numCols = 1;
        int numRows = (static_cast<int>(n) + numCols - 1) / numCols;
        
        float contentHeight = std::max(scrollH, static_cast<float>(numRows) * rowHeight + 10.f);
        m_homeActionsScroll->m_contentLayer->setContentSize(CCSize{scrollW, contentHeight});

        auto scrollMenu = CCMenu::create();
        scrollMenu->setPosition({0.f, 0.f});
        scrollMenu->setContentSize(m_homeActionsScroll->m_contentLayer->getContentSize());
        m_homeActionsScroll->m_contentLayer->addChild(scrollMenu, 10);

        float startX = (scrollW - (static_cast<float>(numCols) * colWidth)) / 2.f;

        for (size_t i = 0; i < n; ++i) {
            auto const& action = actions[i];

            int row = static_cast<int>(i / numCols);
            int col = static_cast<int>(i % numCols);

            float x = startX + col * colWidth + colWidth / 2.f;
            float targetY = contentHeight - (row * rowHeight + rowHeight / 2.f + 5.f);

            ccColor3B borderColor = categories[action.categoryIndex].color;

            // NineSlice canónico (Geode style) en lugar del antiguo
            // CCDrawNode con borde custom. La identidad de color de la
            // categoría se preserva con un acento de 2px arriba de la
            // card en vez de un borde completo.
            auto cardBg = paimon::SpriteHelper::createColorPanel(
                cardW, cardH, cocos2d::ccColor3B{24, 25, 38}, 230, 5.f
            );

            // Acento superior con el color de la categoría (delgado, 2px).
            // Sustituye el borde completo del estilo anterior por un trazo
            // limpio que sigue identificando visualmente cada categoría.
            auto accent = paimon::SpriteHelper::createColorPanel(
                cardW - 14.f, 2.f, borderColor, 220, 0.f
            );
            accent->setPosition({7.f, cardH - 4.f});
            cardBg->addChild(accent, 1);
            // Using neutral bigFont.fnt base white font to elegantly blend in
            auto label = CCLabelBMFont::create(action.title.c_str(), "bigFont.fnt");
            label->setAnchorPoint({0.5f, 0.5f});
            float labelScale = 0.38f;
            label->setScale(labelScale);

            float maxLabelW = cardW - 12.f;
            float currentW = label->getScaledContentSize().width;
            if (currentW > maxLabelW) {
                label->setScale(labelScale * (maxLabelW / currentW));
            }
            label->setPosition({cardW / 2.f, cardH / 2.f});
            cardBg->addChild(label);

            auto btn = CCMenuItemExt::createSpriteExtra(cardBg, [self = WeakRef<PaimonHubLayer>(this), action](CCMenuItemSpriteExtra*) {
                if (auto layer = self.lock()) action.onPress(layer);
            });

            btn->setPosition({x, targetY - 18.f});
            btn->setScale(0.f);
            btn->setOpacity(0);
            scrollMenu->addChild(btn);

            // Elegant micro-animation for entrance
            btn->runAction(CCSequence::create(
                CCDelayTime::create(0.02f * static_cast<float>(i)),
                CCSpawn::create(
                    CCFadeTo::create(0.18f, 255),
                    CCEaseBackOut::create(CCMoveTo::create(0.24f, {x, targetY})),
                    CCScaleTo::create(0.24f, 1.f),
                    nullptr
                ),
                nullptr
            ));
        }
    } else {
        // Lay out in normal category static menu style
        m_homeActionsMenu->setVisible(true);

        for (size_t i = 0; i < n; ++i) {
            auto const& action = actions[i];

            float x = rightPanelCX;
            float targetY = 110.f;

            if (n <= 3) {
                float spacing = 125.f;
                float totalSpan = spacing * static_cast<float>(n - 1);
                float startX = rightPanelCX - totalSpan / 2.f;
                x = startX + static_cast<float>(i) * spacing;
                targetY = 110.f;
            } else {
                float colSpacing = 130.f;
                x = rightPanelCX + (i % 2 == 0 ? -colSpacing / 2.f : colSpacing / 2.f);
                targetY = (i < 2) ? 142.f : 82.f;
            }

            ccColor3B borderColor = categories[action.categoryIndex].color;

            // NineSlice canónico — ver doc en el bloque equivalente de
            // arriba. La identidad de color sigue presente via el acento
            // pequeño que el llamador puede añadir con `borderColor`.
            auto cardBg = paimon::SpriteHelper::createColorPanel(
                112.f, 48.f, cocos2d::ccColor3B{24, 25, 38}, 230, 5.f
            );

            // Acento superior con color de categoría (mismo patrón que
            // el bloque del scroll de arriba).
            auto accent = paimon::SpriteHelper::createColorPanel(
                112.f - 14.f, 2.f, borderColor, 220, 0.f
            );
            accent->setPosition({7.f, 48.f - 4.f});
            cardBg->addChild(accent, 1);
            // Using neutral bigFont.fnt base white font to elegantly blend in
            auto label = CCLabelBMFont::create(action.title.c_str(), "bigFont.fnt");
            label->setAnchorPoint({0.5f, 0.5f});
            float labelScale = 0.38f;
            label->setScale(labelScale);

            float maxLabelW = 112.f - 12.f;
            float currentW = label->getScaledContentSize().width;
            if (currentW > maxLabelW) {
                label->setScale(labelScale * (maxLabelW / currentW));
            }
            label->setPosition({112.f / 2.f, 48.f / 2.f});
            cardBg->addChild(label);

            auto btn = CCMenuItemExt::createSpriteExtra(cardBg, [self = WeakRef<PaimonHubLayer>(this), action](CCMenuItemSpriteExtra*) {
                if (auto layer = self.lock()) action.onPress(layer);
            });

            btn->setPosition({x, targetY - 18.f});
            btn->setScale(0.f);
            btn->setOpacity(0);
            m_homeActionsMenu->addChild(btn);

            btn->runAction(CCSequence::create(
                CCDelayTime::create(0.04f * static_cast<float>(i)),
                CCSpawn::create(
                    CCFadeTo::create(0.18f, 255),
                    CCEaseBackOut::create(CCMoveTo::create(0.24f, {x, targetY})),
                    CCScaleTo::create(0.24f, 1.f),
                    nullptr
                ),
                nullptr
            ));
        }
    }
}

void PaimonHubLayer::rebuildHomeCategorySettings() {}

void PaimonHubLayer::buildNewsTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;
    float panelW = winSize.width - 30.f;
    float panelH = 250.f;

    // 1. Sleek Dashboard Panel background with glowing gold theme border
    auto panelBg = paimon::SpriteHelper::createRoundedRect(
        panelW, panelH, 6.f,
        {10/255.f, 10/255.f, 18/255.f, 245/255.f},       // dark transparent background
        {255/255.f, 200/255.f, 80/255.f, 120/255.f},     // gold theme border
        1.2f
    );
    panelBg->setPosition({15.f, 15.f});
    m_newsTab->addChild(panelBg, 0);

    // 2. Title header (left-aligned dashboard styling)
    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.news.title", "Latest News").c_str(),
        "goldFont.fnt"
    );
    titleLbl->setScale(0.48f);
    titleLbl->setAnchorPoint({0.f, 0.5f});
    titleLbl->setPosition({35.f, 242.f});
    m_newsTab->addChild(titleLbl, 1);

    // Sleek gold divider under title
    auto sep = CCLayerColor::create({255, 200, 80, 50});
    sep->setContentSize({winSize.width - 70.f, 1.f});
    sep->setPosition({35.f, 226.f});
    m_newsTab->addChild(sep, 1);

    // 3. Refresh button aligned with the divider/header row
    auto refreshSpr = ButtonSprite::create(
        tr("pai.hub.news.refresh", "Refresh").c_str(),
        "bigFont.fnt", "GJ_button_06.png", .8f
    );
    refreshSpr->setScale(0.4f);
    auto refreshBtn = CCMenuItemSpriteExtra::create(refreshSpr, this, menu_selector(PaimonHubLayer::onRefreshNews));
    refreshBtn->setPosition({winSize.width - 45.f, 242.f});
    m_newsMenu->addChild(refreshBtn);

    std::vector<std::pair<std::string, std::string>> newsItems = {
        {tr("pai.hub.news.item1.title", "Welcome to Paimon Hub!"),
         tr("pai.hub.news.item1.desc", "Check out our new hub with news and forum sections.")},
        {tr("pai.hub.news.item2.title", "Version 1.1.0 Released"),
         tr("pai.hub.news.item2.desc", "New features and bug fixes are now available.")},
        {tr("pai.hub.news.item3.title", "Custom Profiles"),
         tr("pai.hub.news.item3.desc", "Create and share your custom profile pictures.")},
    };

    // 4. Scrollable container for news items to prevent any future overflow
    float listW = winSize.width - 70.f;
    float listH = 195.f; // spans from y=25.f to y=220.f
    auto scroll = ScrollLayer::create({listW, listH});
    scroll->setPosition({35.f, 25.f});
    scroll->setID("news-scroll"_spr);
    m_newsTab->addChild(scroll, 1);

    float itemW = listW - 8.f; // leave room for scrollbar
    float itemH = 52.f;
    float itemGap = 8.f;
    float totalH = static_cast<float>(newsItems.size()) * (itemH + itemGap) + 6.f;
    if (totalH < listH) totalH = listH;

    scroll->m_contentLayer->setContentSize({listW, totalH});

    float y = totalH;
    for (size_t i = 0; i < newsItems.size(); i++) {
        y -= itemH;

        // Container node for the news card (handles scaling, fading, and nesting cleanly)
        auto cardContainer = cocos2d::CCNodeRGBA::create();
        cardContainer->setContentSize({itemW, itemH});
        cardContainer->setAnchorPoint({0.f, 0.f});
        cardContainer->setPosition({4.f, y});
        cardContainer->setCascadeOpacityEnabled(true);
        scroll->m_contentLayer->addChild(cardContainer, 1);

        // Rounded rect background for each card
        auto cardBg = paimon::SpriteHelper::createRoundedRect(
            itemW, itemH, 5.f,
            {20/255.f, 20/255.f, 32/255.f, 180/255.f},       // dark fill
            {255/255.f, 200/255.f, 80/255.f, 80/255.f},      // gold border
            0.8f
        );
        cardBg->setPosition({0.f, 0.f});
        cardBg->setCascadeOpacityEnabled(true);
        cardContainer->addChild(cardBg, 0);

        // Title text
        auto itemTitle = CCLabelBMFont::create(newsItems[i].first.c_str(), "goldFont.fnt");
        itemTitle->setScale(0.34f);
        itemTitle->setAnchorPoint({0.f, 0.5f});
        itemTitle->setPosition({14.f, itemH - 15.f});
        cardContainer->addChild(itemTitle, 1);

        // Description text
        auto itemDesc = CCLabelBMFont::create(newsItems[i].second.c_str(), "bigFont.fnt");
        itemDesc->setScale(0.24f);
        itemDesc->setColor({180, 180, 180});
        itemDesc->setAnchorPoint({0.f, 0.5f});
        itemDesc->setPosition({14.f, 15.f});
        cardContainer->addChild(itemDesc, 1);

        // Glow green "NEW" Badge
        auto newBadge = CCLabelBMFont::create("NEW", "bigFont.fnt");
        newBadge->setScale(0.24f);
        newBadge->setColor({100, 255, 100});
        newBadge->setAnchorPoint({1.f, 0.5f});
        newBadge->setPosition({itemW - 14.f, itemH - 15.f});
        cardContainer->addChild(newBadge, 1);

        // Cascading spring-based entrance animation
        cardContainer->setScale(0.85f);
        cardContainer->setOpacity(0);
        cardContainer->runAction(CCSequence::create(
            CCDelayTime::create(0.04f * i),
            CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(0.25f, 1.f)),
                CCFadeTo::create(0.2f, 255),
                nullptr
            ),
            nullptr
        ));

        y -= itemGap;
    }
    scroll->scrollToTop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Forum tab — redesigned for clarity and density.
//
// Layout (panel inside the existing CCLayer panel, 250px tall):
//
//   ┌─ Header ─────────────────────────────────────────────────────────┐
//   │  Community Forum    [ Browse | Create ]                          │
//   │  Subtitle / count                                                │
//   ├─ Toolbar ────────────────────────────────────────────────────────┤
//   │  Sort: [Recent] [Top] [Liked]   |   Tags: [Predef] [+]   ↻       │
//   ├─ Active filter chips ────────────────────────────────────────────┤
//   │  ✕ Guide   ✕ Tip                                                 │
//   ├─ Post list (scrollable) ─────────────────────────────────────────┤
//   │  cards…                                                          │
//   └──────────────────────────────────────────────────────────────────┘
//
// The Create panel mirrors the same header/divider chrome so the visual
// rhythm doesn't break when toggling sub-tabs.
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    constexpr float kForumHeaderY     = 242.f;
    constexpr float kForumSubtitleY   = 224.f;
    constexpr float kForumDividerY    = 212.f;
    constexpr float kForumToolbarY    = 196.f;
    constexpr float kForumChipsY      = 174.f;
    constexpr float kForumListTop     = 158.f;
    constexpr float kForumListBottom  = 25.f;

    static constexpr ccColor3B kForumAccent = {120, 170, 255};
    static constexpr ccColor3B kForumAccentSoft = {80, 130, 220};

    static CCNode* makeForumPill(
        char const* text,
        char const* bg,
        float scale,
        cocos2d::SEL_MenuHandler handler,
        cocos2d::CCObject* target,
        int tag = 0
    ) {
        auto spr = ButtonSprite::create(text, "bigFont.fnt", bg, .8f);
        spr->setScale(scale);
        auto btn = CCMenuItemSpriteExtra::create(spr, target, handler);
        btn->setTag(tag);
        return btn;
    }
}

void PaimonHubLayer::buildForumTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;
    float panelW = winSize.width - 30.f;
    float panelH = 250.f;
    float panelLeft = 15.f;
    float panelRight = panelLeft + panelW;
    float contentLeft = panelLeft + 18.f;
    float contentRight = panelRight - 18.f;

    // ── Background panel (matches Home/News chrome) ─────────────────────────
    auto panel = paimon::SpriteHelper::createRoundedRect(
        panelW, panelH, 6.f,
        {10/255.f, 10/255.f, 18/255.f, 245/255.f},
        {kForumAccent.r/255.f, kForumAccent.g/255.f, kForumAccent.b/255.f, 120/255.f},
        1.2f
    );
    panel->setPosition({panelLeft, 15.f});
    m_forumTab->addChild(panel, 0);

    // ── Sub-tab pill switcher ───────────────────────────────────────────────
    // Lives inside m_forumTab (NOT m_forumMenu) so it inherits the tab's
    // visibility cycle and stays at a known z order relative to the panel.
    {
        auto subBar = CCMenu::create();
        subBar->setID("forum-subtabs"_spr);
        subBar->setContentSize({200.f, 30.f});
        subBar->setAnchorPoint({1.f, 0.5f});
        subBar->setPosition({contentRight, 242.f});
        subBar->setLayout(
            RowLayout::create()
                ->setGap(6.f)
                ->setAutoScale(false)
                ->setAxisAlignment(AxisAlignment::End)
        );
        m_forumTab->addChild(subBar, 12);

        struct SubTabSpec {
            const char* key;
            const char* label;
            const char* sprite;
            const char* font;
        };
        std::array<SubTabSpec, 2> subs = {{
            {"pai.hub.forum.browse",     "Browse",     "GJ_button_04.png", "bigFont.fnt"},
            {"pai.hub.forum.create.cta", "+ New Post", "GJ_button_01.png", "goldFont.fnt"},
        }};

        m_forumSubTabBtns.clear();
        for (size_t i = 0; i < subs.size(); ++i) {
            auto spr = ButtonSprite::create(
                tr(subs[i].key, subs[i].label).c_str(),
                subs[i].font, subs[i].sprite, .8f
            );
            spr->setScale(0.36f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(PaimonHubLayer::onForumSubTabSwitch)
            );
            btn->setTag(static_cast<int>(i));
            subBar->addChild(btn);
            m_forumSubTabBtns.push_back(btn);
        }
        subBar->updateLayout();
    }

    // ── Shared header: title + subtitle + divider ───────────────────────────
    // These live on m_forumTab and just have their strings swapped when the
    // sub-tab toggles. Avoids the previous bug where the Browse header text
    // bled into the Create view because it lived on m_forumTab unconditionally.
    m_forumHeaderTitle = CCLabelBMFont::create(
        tr("pai.hub.forum.title", "Community Forum").c_str(),
        "goldFont.fnt"
    );
    m_forumHeaderTitle->setScale(0.5f);
    m_forumHeaderTitle->setAnchorPoint({0.f, 0.5f});
    m_forumHeaderTitle->setPosition({contentLeft, kForumHeaderY});
    m_forumTab->addChild(m_forumHeaderTitle, 1);

    m_forumHeaderSubtitle = CCLabelBMFont::create(
        tr("pai.hub.forum.subtitle",
            "Share guides, tips and showcases with the community.").c_str(),
        "bigFont.fnt"
    );
    m_forumHeaderSubtitle->setScale(0.26f);
    m_forumHeaderSubtitle->setColor({170, 180, 210});
    m_forumHeaderSubtitle->setAnchorPoint({0.f, 0.5f});
    m_forumHeaderSubtitle->setPosition({contentLeft, kForumSubtitleY});
    m_forumTab->addChild(m_forumHeaderSubtitle, 1);

    auto sep = CCLayerColor::create({kForumAccent.r, kForumAccent.g, kForumAccent.b, 70});
    sep->setContentSize({panelW - 36.f, 1.f});
    sep->setPosition({contentLeft, kForumDividerY});
    m_forumTab->addChild(sep, 1);

    // ─── Browse section ─────────────────────────────────────────────────────
    m_forumBrowseNode = CCNode::create();
    m_forumBrowseNode->setPosition({0, 0});
    m_forumBrowseNode->setContentSize(winSize);
    m_forumTab->addChild(m_forumBrowseNode, 1);

    auto browseMenu = CCMenu::create();
    browseMenu->setPosition({0, 0});
    browseMenu->setContentSize(winSize);
    browseMenu->setID("forum-browse-menu"_spr);
    m_forumBrowseNode->addChild(browseMenu, 2);

    // Toolbar — Sort cluster + Tag cluster + Refresh icon, balanced widths
    {
        auto sortLabel = CCLabelBMFont::create(
            tr("pai.hub.forum.sort", "Sort:").c_str(), "bigFont.fnt"
        );
        sortLabel->setScale(0.32f);
        sortLabel->setColor({200, 210, 230});
        sortLabel->setAnchorPoint({0.f, 0.5f});
        sortLabel->setPosition({contentLeft, kForumToolbarY});
        m_forumBrowseNode->addChild(sortLabel, 1);

        auto sortMenu = CCMenu::create();
        sortMenu->setID("forum-sort"_spr);
        sortMenu->setContentSize({170.f, 24.f});
        sortMenu->setAnchorPoint({0.f, 0.5f});
        sortMenu->setPosition({contentLeft + 32.f, kForumToolbarY});
        sortMenu->setLayout(
            RowLayout::create()
                ->setGap(4.f)
                ->setAutoScale(false)
                ->setAxisAlignment(AxisAlignment::Start)
        );
        m_forumBrowseNode->addChild(sortMenu, 1);

        std::array<std::pair<const char*, const char*>, 3> sortOptions = {{
            {"pai.hub.forum.sort.recent", "Recent"},
            {"pai.hub.forum.sort.top",    "Top"},
            {"pai.hub.forum.sort.liked",  "Liked"},
        }};
        m_sortBtns.clear();
        for (size_t i = 0; i < sortOptions.size(); ++i) {
            auto btn = makeForumPill(
                tr(sortOptions[i].first, sortOptions[i].second).c_str(),
                "GJ_button_04.png", 0.32f,
                menu_selector(PaimonHubLayer::onSortChanged), this,
                static_cast<int>(i)
            );
            sortMenu->addChild(btn);
            m_sortBtns.push_back(static_cast<CCMenuItemSpriteExtra*>(btn));
        }
        sortMenu->updateLayout();

        for (size_t i = 0; i < m_sortBtns.size(); ++i) {
            if (auto spr = typeinfo_cast<ButtonSprite*>(m_sortBtns[i]->getNormalImage())) {
                spr->setColor(static_cast<int>(m_sortMode) == (int)i
                    ? ccColor3B{100, 255, 100}
                    : ccColor3B{200, 210, 230});
            }
        }

        // Vertical separator between clusters
        auto vsep = CCLayerColor::create({kForumAccent.r, kForumAccent.g, kForumAccent.b, 60});
        vsep->setContentSize({1.f, 18.f});
        vsep->setPosition({cx + 30.f, kForumToolbarY - 9.f});
        m_forumBrowseNode->addChild(vsep, 1);

        // Tag cluster (right of separator)
        auto tagLabel = CCLabelBMFont::create(
            tr("pai.hub.forum.tags", "Tags:").c_str(), "bigFont.fnt"
        );
        tagLabel->setScale(0.32f);
        tagLabel->setColor({200, 210, 230});
        tagLabel->setAnchorPoint({0.f, 0.5f});
        tagLabel->setPosition({cx + 44.f, kForumToolbarY});
        m_forumBrowseNode->addChild(tagLabel, 1);

        auto pickPredefBtn = makeForumPill(
            tr("pai.hub.forum.predef", "Predef").c_str(),
            "GJ_button_05.png", 0.32f,
            menu_selector(PaimonHubLayer::onOpenPredefPicker), this
        );
        pickPredefBtn->setPosition({cx + 100.f, kForumToolbarY});
        browseMenu->addChild(pickPredefBtn);

        auto createTagBtn = makeForumPill(
            "+", "GJ_button_06.png", 0.42f,
            menu_selector(PaimonHubLayer::onCreateTag), this
        );
        createTagBtn->setPosition({cx + 142.f, kForumToolbarY});
        browseMenu->addChild(createTagBtn);

        // Refresh icon — anchored to the toolbar's right edge
        if (auto refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png")) {
            refreshSpr->setScale(0.5f);
            auto refreshBtn = CCMenuItemExt::createSpriteExtra(
                refreshSpr,
                [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
                    if (auto layer = self.lock()) layer->refreshForumPosts();
                }
            );
            refreshBtn->setID("forum-refresh"_spr);
            refreshBtn->setPosition({contentRight - 8.f, kForumToolbarY});
            browseMenu->addChild(refreshBtn);
        }
    }

    // ── Active filter chips row ─────────────────────────────────────────────
    float tagAreaW = panelW - 36.f;
    float tagAreaH = 22.f;

    m_tagMenu = CCMenu::create();
    m_tagMenu->setID("forum-tags"_spr);
    m_tagMenu->setContentSize({tagAreaW, tagAreaH});
    m_tagMenu->setAnchorPoint({0.5f, 0.5f});
    m_tagMenu->setPosition({cx, kForumChipsY});
    m_tagMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_forumBrowseNode->addChild(m_tagMenu, 1);

    m_emptyTagsHint = CCLabelBMFont::create(
        tr("pai.hub.forum.tags.empty",
           "No active filter — tap [Predef] or [+] to add tags").c_str(),
        "bigFont.fnt"
    );
    m_emptyTagsHint->setScale(0.26f);
    m_emptyTagsHint->setColor({140, 140, 160});
    m_emptyTagsHint->setPosition({cx, kForumChipsY});
    m_forumBrowseNode->addChild(m_emptyTagsHint, 1);

    refreshTagButtons();

    // ── Post list ───────────────────────────────────────────────────────────
    float listW = panelW - 24.f;
    float listH = kForumListTop - kForumListBottom;

    m_noPostsLabel = CCLabelBMFont::create(
        tr("pai.hub.forum.no_posts",
           "No posts here yet — tap +New Post to start the conversation.").c_str(),
        "bigFont.fnt"
    );
    m_noPostsLabel->setScale(0.32f);
    m_noPostsLabel->setColor({150, 150, 170});
    m_noPostsLabel->setPosition({cx, kForumListBottom + listH / 2.f});
    m_forumBrowseNode->addChild(m_noPostsLabel, 1);

    m_forumPostList = CCNode::create();
    m_forumPostList->setPosition({cx, kForumListTop});
    m_forumPostList->setContentSize({listW, listH});
    m_forumBrowseNode->addChild(m_forumPostList, 2);

    refreshForumPosts();

    // ─── Inline Create form ────────────────────────────────────────────────
    m_forumCreateNode = CCNode::create();
    m_forumCreateNode->setPosition({0, 0});
    m_forumCreateNode->setContentSize(winSize);
    m_forumCreateNode->setVisible(false);
    m_forumTab->addChild(m_forumCreateNode, 1);

    // Title input row
    auto lblTitle = CCLabelBMFont::create("Title", "bigFont.fnt");
    lblTitle->setScale(0.30f);
    lblTitle->setAnchorPoint({0.f, 0.5f});
    lblTitle->setPosition({contentLeft, 198.f});
    lblTitle->setColor({200, 210, 230});
    m_forumCreateNode->addChild(lblTitle, 1);

    m_createTitleInput = TextInput::create(panelW - 36.f, "Post title...", "chatFont.fnt");
    m_createTitleInput->setCommonFilter(CommonFilter::Any);
    m_createTitleInput->setMaxCharCount(80);
    m_createTitleInput->setPosition({cx, 180.f});
    m_createTitleInput->setScale(0.85f);
    m_forumCreateNode->addChild(m_createTitleInput, 1);

    // Description input
    auto lblDesc = CCLabelBMFont::create("Description", "bigFont.fnt");
    lblDesc->setScale(0.30f);
    lblDesc->setAnchorPoint({0.f, 0.5f});
    lblDesc->setPosition({contentLeft, 158.f});
    lblDesc->setColor({200, 210, 230});
    m_forumCreateNode->addChild(lblDesc, 1);

    m_createDescInput = TextInput::create(panelW - 36.f, "Description...", "chatFont.fnt");
    m_createDescInput->setCommonFilter(CommonFilter::Any);
    m_createDescInput->setMaxCharCount(500);
    m_createDescInput->setPosition({cx, 140.f});
    m_createDescInput->setScale(0.85f);
    m_forumCreateNode->addChild(m_createDescInput, 1);

    // Tags
    auto lblTags = CCLabelBMFont::create("Tags (tap to toggle)", "bigFont.fnt");
    lblTags->setScale(0.28f);
    lblTags->setAnchorPoint({0.f, 0.5f});
    lblTags->setPosition({contentLeft, 118.f});
    lblTags->setColor({200, 210, 230});
    m_forumCreateNode->addChild(lblTags, 1);

    auto predefMenu = CCMenu::create();
    predefMenu->setContentSize({panelW - 36.f, 38.f});
    predefMenu->setAnchorPoint({0.5f, 1.f});
    predefMenu->setPosition({cx, 110.f});
    predefMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(true)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
    );
    m_forumCreateNode->addChild(predefMenu, 1);

    for (size_t i = 0; i < m_forumTags.size(); ++i) {
        auto chipSpr = ButtonSprite::create(
            m_forumTags[i].c_str(), "bigFont.fnt", "GJ_button_05.png", .8f
        );
        chipSpr->setScale(0.26f);
        auto chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonHubLayer::onCreateToggleTag)
        );
        chipBtn->setTag(static_cast<int>(i));
        predefMenu->addChild(chipBtn);
    }
    predefMenu->updateLayout();

    // Selected tags hint (single line — kept readable, no overlap)
    m_createTagsHint = CCLabelBMFont::create(
        "Tap a tag above to attach it to your post", "bigFont.fnt"
    );
    m_createTagsHint->setScale(0.26f);
    m_createTagsHint->setColor({140, 140, 160});
    m_createTagsHint->setPosition({cx, 64.f});
    m_forumCreateNode->addChild(m_createTagsHint, 1);

    // Selected tags container is tiny — used only as legacy storage to keep
    // existing onCreateToggleTag wiring working without API changes. It
    // intentionally has no visible children to avoid a duplicate row of chips.
    m_createTagMenu = CCMenu::create();
    m_createTagMenu->setID("inline-create-tags"_spr);
    m_createTagMenu->setVisible(false);
    m_forumCreateNode->addChild(m_createTagMenu, 1);

    // Submit + Cancel buttons (centered, larger, gold post)
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    btnMenu->setContentSize(winSize);
    m_forumCreateNode->addChild(btnMenu, 1);

    auto cancelSpr = ButtonSprite::create(
        tr("pai.hub.forum.cancel", "Cancel").c_str(),
        "bigFont.fnt", "GJ_button_06.png", 0.8f
    );
    cancelSpr->setScale(0.5f);
    auto cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(PaimonHubLayer::onForumSubTabSwitch)
    );
    cancelBtn->setTag(0);
    cancelBtn->setPosition({cx - 70.f, 32.f});
    btnMenu->addChild(cancelBtn);

    auto submitSpr = ButtonSprite::create(
        tr("pai.hub.forum.publish", "Publish").c_str(),
        "goldFont.fnt", "GJ_button_01.png", 0.9f
    );
    submitSpr->setScale(0.6f);
    auto submitBtn = CCMenuItemSpriteExtra::create(
        submitSpr, this, menu_selector(PaimonHubLayer::onCreateSubmit)
    );
    submitBtn->setPosition({cx + 70.f, 32.f});
    btnMenu->addChild(submitBtn);

    // highlight default sub-tab (Browse)
    for (size_t i = 0; i < m_forumSubTabBtns.size(); ++i) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_forumSubTabBtns[i]->getNormalImage())) {
            spr->setColor(i == 0 ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
        }
    }
}

void PaimonHubLayer::onOpenConfig(CCObject*) {
    // pushScene directo (sin TransitionManager) para evitar wrapping doble
    // de transiciones que dejaba la pantalla negra al volver con popSceneWithTransition.
    auto scene = PaiConfigLayer::scene();
    if (scene) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenProfiles(CCObject*) {
    auto popup = ProfilePicEditorPopup::create();
    popup->show();
}

void PaimonHubLayer::onOpenBackgrounds(CCObject*) {
    auto popup = BackgroundConfigPopup::create();
    popup->show();
}

void PaimonHubLayer::onOpenPaiDraw(CCObject*) {
    paimon::storeButtonOrigin({CCDirector::get()->getWinSize().width - 50.f, 54.f});
    auto scene = paidraw::PaiDrawLobbyLayer::scene();
    if (scene) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenExtras(CCObject*) {
    auto scene = PaiConfigLayer::scene();
    if (scene) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenSupport(CCObject*) {
    // PaimonSupportLayer::onBack hace popSceneWithTransition; pushScene directo deja
    // el Hub correctamente en el stack para que el pop lo restaure sin pantalla negra.
    auto scene = PaimonSupportLayer::scene();
    if (scene) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenDiscordConfig(CCObject*) {
    if (auto popup = paimon::discord::DiscordConfigPopup::create()) {
        popup->show();
    }
}

void PaimonHubLayer::onBack(CCObject*) {
    CCDirector::get()->replaceScene(MenuLayer::scene(false));
}

void PaimonHubLayer::update(float dt) {
    CCLayer::update(dt);
}

void PaimonHubLayer::onCheckUpdate(CCObject*) {
    auto& checker = paimon::updates::UpdateChecker::get();
    auto winSize = CCDirector::get()->getWinSize();

    auto flash = [&](char const* text, cocos2d::ccColor3B color) {
        auto msg = CCLabelBMFont::create(text, "bigFont.fnt");
        msg->setScale(0.45f);
        msg->setPosition({winSize.width / 2, winSize.height / 2});
        msg->setColor(color);
        this->addChild(msg, 100);
        msg->runAction(CCSequence::create(
            CCDelayTime::create(1.8f),
            CCRemoveSelf::create(),
            nullptr
        ));
    };

    switch (checker.state()) {
        case paimon::updates::UpdateChecker::State::Idle:
        case paimon::updates::UpdateChecker::State::Failed:
            checker.checkAsync();
            flash(tr("pai.update.checking", "Checking for updates...").c_str(), {100, 200, 255});
            return;

        case paimon::updates::UpdateChecker::State::Checking:
            flash(tr("pai.update.checking", "Checking for updates...").c_str(), {100, 200, 255});
            return;

        case paimon::updates::UpdateChecker::State::UpdateAvailable:
            if (checker.downloadUrl().empty()) {
                flash(tr("pai.update.failed", "Error: no download URL").c_str(), {255, 110, 110});
                return;
            }
            {
                auto popup = paimon::updates::UpdateProgressPopup::create();
                if (popup) popup->show();
            }
            return;

        case paimon::updates::UpdateChecker::State::UpToDate:
        default:
            flash(tr("pai.update.uptodate", "You're up to date!").c_str(), {120, 255, 120});
            return;
    }
}

void PaimonHubLayer::refreshUpdateButton() {
    // El acceso a actualizaciones ahora vive dentro de las tarjetas dinamicas del hub.
}

void PaimonHubLayer::onRefreshNews(CCObject*) {
    auto winSize = CCDirector::get()->getWinSize();
    auto successLbl = CCLabelBMFont::create(
        tr("pai.hub.news.refreshed", "News refreshed!").c_str(),
        "bigFont.fnt"
    );
    successLbl->setScale(0.35f);
    successLbl->setPosition({winSize.width / 2, winSize.height / 2});
    successLbl->setColor({100, 255, 100});
    this->addChild(successLbl, 100);

    successLbl->runAction(CCSequence::create(
        CCDelayTime::create(1.5f),
        CCRemoveSelf::create(),
        nullptr
    ));
}

void PaimonHubLayer::onCreatePost(CCObject*) {
    // Compone lista de tags disponibles: predef + custom
    std::vector<std::string> available;
    for (auto const& t : m_forumTags)  available.push_back(t);
    for (auto const& t : m_customTags) {
        if (std::find(available.begin(), available.end(), t) == available.end())
            available.push_back(t);
    }

    auto popup = CreatePostPopup::create(
        std::move(available),
        [self = WeakRef<PaimonHubLayer>(this)](paimon::forum::Post const& p) {
            auto layer = self.lock();
            if (!layer) return;
            // sincroniza tags nuevos al hub
            for (auto const& t : p.tags) {
                bool inPredef = std::find(layer->m_forumTags.begin(), layer->m_forumTags.end(), t) != layer->m_forumTags.end();
                bool inCustom = std::find(layer->m_customTags.begin(), layer->m_customTags.end(), t) != layer->m_customTags.end();
                if (!inPredef && !inCustom) layer->m_customTags.push_back(t);
            }
            layer->refreshTagButtons();
            layer->refreshForumPosts();
        }
    );
    if (popup) popup->show();
}

void PaimonHubLayer::onCloseCreatePost(CCObject*) {
    // legacy no-op; CreatePostPopup maneja su propio ciclo de vida
}

void PaimonHubLayer::onSubmitPost(CCObject*) {
    // legacy no-op; CreatePostPopup invoca ForumApi::createPost directamente
}

void PaimonHubLayer::onFilterByTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx >= 0 && idx < (int)m_visibleTags.size()) {
        std::string tag = m_visibleTags[idx];
        auto it = std::find(m_selectedTags.begin(), m_selectedTags.end(), tag);
        if (it != m_selectedTags.end()) {
            m_selectedTags.erase(it);
        } else {
            m_selectedTags.push_back(tag);
        }
    }
    refreshTagButtons();
    refreshForumPosts();
}

void PaimonHubLayer::onCreateTag(CCObject*) {
    auto winSize = CCDirector::get()->getWinSize();

    auto overlay = CCLayerColor::create(ccc4(0, 0, 0, 180));
    overlay->setContentSize(winSize);
    overlay->setPosition({0, 0});
    overlay->setID("create-tag-overlay"_spr);
    this->addChild(overlay, 50);
    m_createTagOverlay = overlay;

    auto panel = paimon::SpriteHelper::createDarkPanel(250, 120, 60);
    panel->setPosition({winSize.width / 2 - 125, winSize.height / 2 - 60});
    overlay->addChild(panel);

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.create_tag", "Create Custom Tag").c_str(),
        "goldFont.fnt"
    );
    titleLbl->setScale(0.35f);
    titleLbl->setPosition({winSize.width / 2, winSize.height / 2 + 35});
    overlay->addChild(titleLbl, 1);

    m_newTagInput = TextInput::create(180, tr("pai.hub.forum.new_tag", "Tag name"));
    m_newTagInput->setPosition({winSize.width / 2 - 90, winSize.height / 2});
    m_newTagInput->setScale(0.6f);
    overlay->addChild(m_newTagInput, 51);

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    btnMenu->setContentSize(winSize);
    btnMenu->setID("create-tag-menu"_spr);
    overlay->addChild(btnMenu, 52);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.5f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(PaimonHubLayer::onCloseCreateTag));
    closeBtn->setPosition({winSize.width / 2 + 105, winSize.height / 2 + 45});
    btnMenu->addChild(closeBtn);

    auto createSpr = ButtonSprite::create(
        tr("pai.hub.forum.create", "Create").c_str(),
        "goldFont.fnt", "GJ_button_01.png", .8f
    );
    createSpr->setScale(0.4f);
    auto createBtn = CCMenuItemSpriteExtra::create(createSpr, this, menu_selector(PaimonHubLayer::onSubmitTag));
    createBtn->setPosition({winSize.width / 2, winSize.height / 2 - 35});
    btnMenu->addChild(createBtn);
}

void PaimonHubLayer::onCloseCreateTag(CCObject*) {
    if (m_createTagOverlay) {
        m_createTagOverlay->removeFromParent();
        m_createTagOverlay = nullptr;
        m_newTagInput = nullptr;
    }
}

void PaimonHubLayer::onSubmitTag(CCObject*) {
    std::string tag = m_newTagInput->getString();
    if (!tag.empty()) {
        bool exists = false;
        for (const auto& t : m_forumTags) {
            if (t == tag) {
                exists = true;
                break;
            }
        }
        for (const auto& t : m_customTags) {
            if (t == tag) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_customTags.push_back(tag);
            refreshTagButtons();
        }
    }
    if (m_createTagOverlay) {
        m_createTagOverlay->removeFromParent();
        m_createTagOverlay = nullptr;
        m_newTagInput = nullptr;
    }
}

void PaimonHubLayer::onViewPost(CCObject*) {
}

void PaimonHubLayer::showForumLoading() {
    if (m_forumLoadingSpinner) return;
    m_forumLoadingSpinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    m_forumLoadingSpinner->show(m_forumTab, 100);
}

void PaimonHubLayer::hideForumLoading() {
    if (m_forumLoadingSpinner) {
        m_forumLoadingSpinner->dismiss();
        m_forumLoadingSpinner = nullptr;
    }
}

void PaimonHubLayer::refreshForumPosts() {
    if (!m_forumPostList) return;
    m_forumPostList->removeAllChildren();
    showForumLoading();

    // Construye filtro desde estado actual del hub
    paimon::forum::ListFilter filter;
    switch (m_sortMode) {
        case SortMode::TopRated:  filter.sort = paimon::forum::SortMode::TopRated;  break;
        case SortMode::MostLiked: filter.sort = paimon::forum::SortMode::MostLiked; break;
        case SortMode::Recent:
        default:                  filter.sort = paimon::forum::SortMode::Recent;    break;
    }
    filter.tags = m_selectedTags;
    filter.limit = 50;

    WeakRef<PaimonHubLayer> self = this;
    paimon::forum::ForumApi::get().listPosts(filter,
        [self](paimon::forum::Result<std::vector<paimon::forum::Post>> res) {
            auto layer = self.lock();
            if (!layer || !layer->m_forumPostList) return;
            layer->hideForumLoading();
            layer->renderPosts(res.ok ? res.data : std::vector<paimon::forum::Post>{});
        });
}

void PaimonHubLayer::renderPosts(std::vector<paimon::forum::Post> const& posts) {
    if (!m_forumPostList) return;
    m_forumPostList->removeAllChildren();

    bool hasPosts = !posts.empty();
    if (m_noPostsLabel) m_noPostsLabel->setVisible(!hasPosts);
    if (!hasPosts) return;

    auto* gm = GameManager::get();

    float listW = m_forumPostList->getContentSize().width;
    float listH = m_forumPostList->getContentSize().height;
    if (listH <= 0.f) listH = 180.f;

    constexpr float kPostH = 70.f;
    constexpr float kPostGap = 8.f;
    constexpr float kIconSize = 26.f;
    constexpr float kPad = 10.f;
    float totalH = static_cast<float>(posts.size()) * (kPostH + kPostGap) + 6.f;
    if (totalH < listH) totalH = listH;

    auto scroll = ScrollLayer::create({listW, listH});
    scroll->setPosition({-listW / 2.f, -listH});
    scroll->setID("forum-scroll"_spr);
    m_forumPostList->addChild(scroll, 1);
    scroll->m_contentLayer->setContentSize({listW, totalH});

    float cardW = listW - 8.f;
    float cardX = 4.f;

    float y = totalH;
    int i = 0;
    for (auto const& post : posts) {
        y -= kPostH;

        // ── Card container ────────────────────────────────────────────────
        auto cardContainer = cocos2d::CCNodeRGBA::create();
        cardContainer->setContentSize({cardW, kPostH});
        cardContainer->setAnchorPoint({0.f, 0.f});
        cardContainer->setPosition({cardX, y});
        cardContainer->setCascadeOpacityEnabled(true);
        scroll->m_contentLayer->addChild(cardContainer, 1);

        // Background — solid dark fill with a thin light outline so the
        // cards mimic the reference screenshot (dark interior, hairline white
        // border) without breaking the rest of the panel chrome.
        auto bg = paimon::SpriteHelper::createRoundedRect(
            cardW, kPostH, 6.f,
            {16/255.f, 18/255.f, 28/255.f, 245/255.f},
            {235/255.f, 240/255.f, 255/255.f, 220/255.f},
            1.0f
        );
        if (bg) {
            bg->setPosition({0.f, 0.f});
            bg->setCascadeOpacityEnabled(true);
            cardContainer->addChild(bg, 0);
        }

        // Whole-card click target — opens the detail popup
        auto cardMenu = CCMenu::create();
        cardMenu->setPosition({0, 0});
        cardMenu->setContentSize({cardW, kPostH});
        cardMenu->ignoreAnchorPointForPosition(true);
        cardContainer->addChild(cardMenu, 4);

        WeakRef<PaimonHubLayer> selfRef = this;
        std::string postId = post.id;
        auto openPostDetail = [selfRef, postId]() {
            paimon::forum::ForumApi::get().getPost(postId,
                [selfRef](paimon::forum::Result<paimon::forum::Post> res) {
                    auto layer = selfRef.lock();
                    if (!layer || !res.ok) return;
                    auto popup = PostDetailPopup::create(res.data, [selfRef]() {
                        if (auto l = selfRef.lock()) l->refreshForumPosts();
                    });
                    if (popup) popup->show();
                });
        };

        {
            auto hitArea = CCNode::create();
            hitArea->setContentSize({cardW, kPostH});
            auto hitBtn = CCMenuItemExt::createSpriteExtra(
                hitArea,
                [openPostDetail](CCMenuItemSpriteExtra*) { openPostDetail(); }
            );
            hitBtn->setPosition({cardW / 2.f, kPostH / 2.f});
            cardMenu->addChild(hitBtn);
        }

        // ── Author avatar (left) ──────────────────────────────────────────
        int iconID = std::max(1, post.author.iconID);
        if (auto* player = SimplePlayer::create(iconID)) {
            if (post.author.iconType > 0) {
                player->updatePlayerFrame(iconID, static_cast<IconType>(post.author.iconType));
            }
            if (gm) {
                auto col1 = gm->colorForIdx(post.author.color1);
                auto col2 = gm->colorForIdx(post.author.color2);
                player->setColor(col1);
                player->setSecondColor(col2);
                if (post.author.glowEnabled) player->setGlowOutline(col2);
                else                          player->disableGlowOutline();
            }
            float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
            float gdRefSize = 30.f;
            float scale = (maxDim > 10.f && maxDim < 80.f) ? (kIconSize / maxDim) : (kIconSize / gdRefSize);
            player->setScale(std::max(scale, 0.55f));
            player->setPosition({kPad + kIconSize / 2.f, kPostH - kPad - kIconSize / 2.f});
            cardContainer->addChild(player, 5);
        }

        // ── Author name + relative time (header row) ──────────────────────
        float headerY = kPostH - kPad - kIconSize / 2.f;
        auto nameLbl = CCLabelBMFont::create(
            post.author.username.empty() ? "Anonymous" : post.author.username.c_str(),
            "goldFont.fnt"
        );
        nameLbl->setScale(0.36f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({kPad + kIconSize + 8.f, headerY});
        cardContainer->addChild(nameLbl, 1);

        auto dateLbl = CCLabelBMFont::create(
            paimon::forum::formatRelativeTime(post.createdAt).c_str(),
            "chatFont.fnt"
        );
        dateLbl->setScale(0.42f);
        dateLbl->setColor({150, 160, 185});
        dateLbl->setAnchorPoint({1.f, 0.5f});
        dateLbl->setPosition({cardW - kPad, headerY});
        cardContainer->addChild(dateLbl, 1);

        // ── Title ─────────────────────────────────────────────────────────
        float titleY = kPostH - kPad - kIconSize - 4.f;
        auto titleLbl = CCLabelBMFont::create(
            post.title.empty() ? "(untitled)" : post.title.c_str(),
            "bigFont.fnt"
        );
        titleLbl->setScale(0.40f);
        titleLbl->setAnchorPoint({0.f, 0.5f});
        titleLbl->setPosition({kPad, titleY});
        float titleMaxW = cardW - kPad * 2.f;
        if (titleLbl->getScaledContentSize().width > titleMaxW) {
            titleLbl->setScale(titleLbl->getScale() * titleMaxW / titleLbl->getScaledContentSize().width);
        }
        cardContainer->addChild(titleLbl, 1);

        // ── Description preview ───────────────────────────────────────────
        std::string preview = post.description;
        if (preview.size() > 110) preview = preview.substr(0, 107) + "...";
        if (!preview.empty()) {
            auto pre = CCLabelBMFont::create(preview.c_str(), "chatFont.fnt");
            pre->setScale(0.38f);
            pre->setColor({200, 205, 225});
            pre->setAnchorPoint({0.f, 0.5f});
            pre->setPosition({kPad, titleY - 12.f});
            float preMaxW = cardW - kPad * 2.f;
            if (pre->getScaledContentSize().width > preMaxW) {
                pre->setScale(pre->getScale() * preMaxW / pre->getScaledContentSize().width);
            }
            cardContainer->addChild(pre, 1);
        }

        // ── Footer: tag chips (left) + stats (right) ──────────────────────
        float footerY = kPad + 4.f;
        float tagX = kPad;
        int tagsShown = 0;
        for (auto const& t : post.tags) {
            if (tagsShown >= 3) break;
            auto chip = ButtonSprite::create(t.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.7f);
            chip->setScale(0.20f);
            chip->setAnchorPoint({0.f, 0.5f});
            chip->setPosition({tagX, footerY});
            cardContainer->addChild(chip, 2);
            tagX += chip->getScaledContentSize().width + 4.f;
            ++tagsShown;
        }
        if ((int)post.tags.size() > tagsShown) {
            auto more = CCLabelBMFont::create(
                fmt::format("+{}", static_cast<int>(post.tags.size()) - tagsShown).c_str(),
                "chatFont.fnt"
            );
            more->setScale(0.42f);
            more->setColor({150, 160, 185});
            more->setAnchorPoint({0.f, 0.5f});
            more->setPosition({tagX, footerY});
            cardContainer->addChild(more, 2);
        }

        // Stats: ♥ likes • 💬 replies — using bullet text since BMFont lacks emoji glyphs
        auto stats = CCLabelBMFont::create(
            fmt::format("{}  Likes  -  {}  Replies", post.likes, post.replyCount).c_str(),
            "bigFont.fnt"
        );
        stats->setScale(0.28f);
        stats->setColor(post.likedByMe ? ccColor3B{255, 140, 170} : ccColor3B{200, 220, 255});
        stats->setAnchorPoint({1.f, 0.5f});
        stats->setPosition({cardW - kPad, footerY});
        cardContainer->addChild(stats, 1);

        // Cascading entrance animation
        float delay = std::min(0.04f * i, 0.4f);
        cardContainer->setScale(0.92f);
        cardContainer->setOpacity(0);
        cardContainer->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.f)),
                CCFadeTo::create(0.18f, 255),
                nullptr
            ),
            nullptr
        ));

        y -= kPostGap;
        ++i;
    }

    scroll->scrollToTop();
}

void PaimonHubLayer::refreshTagButtons() {
    if (!m_tagMenu) return;
    m_tagMenu->removeAllChildren();
    m_visibleTags.clear();

    for (auto const& t : m_activePredefTags) m_visibleTags.push_back(t);
    for (auto const& t : m_customTags)       m_visibleTags.push_back(t);

    // Show the hint when there are no tags available *at all* — not just when
    // none are selected. Otherwise the hint stays hidden after activating a
    // predefined tag, leaving the chip row empty without explanation.
    bool hasAny = !m_visibleTags.empty();
    if (m_emptyTagsHint) {
        m_emptyTagsHint->setVisible(!hasAny);
    }

    for (size_t i = 0; i < m_visibleTags.size(); i++) {
        bool isSelected = std::find(m_selectedTags.begin(), m_selectedTags.end(),
            m_visibleTags[i]) != m_selectedTags.end();

        // Selected chips get the × suffix so users see the toggle at a glance.
        std::string label = isSelected
            ? (m_visibleTags[i] + "  x")
            : m_visibleTags[i];

        auto tagSpr = ButtonSprite::create(
            label.c_str(), "bigFont.fnt",
            isSelected ? "GJ_button_01.png" : "GJ_button_04.png", .8f
        );
        tagSpr->setScale(0.32f);
        if (!isSelected) tagSpr->setColor({210, 220, 240});
        auto tagBtn = CCMenuItemSpriteExtra::create(
            tagSpr, this, menu_selector(PaimonHubLayer::onFilterByTag)
        );
        tagBtn->setTag(static_cast<int>(i));
        m_tagMenu->addChild(tagBtn);
    }
    m_tagMenu->updateLayout();
}

void PaimonHubLayer::onSortChanged(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx > 2) return;
    m_sortMode = static_cast<SortMode>(idx);

    for (size_t i = 0; i < m_sortBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_sortBtns[i]->getNormalImage())) {
            spr->setColor(idx == (int)i
                ? ccColor3B{100, 255, 100}
                : ccColor3B{200, 210, 230});
        }
    }

    refreshForumPosts();
}

void PaimonHubLayer::onOpenPredefPicker(CCObject*) {
    if (m_predefPickerOverlay) return;

    auto winSize = CCDirector::get()->getWinSize();

    m_predefPickerOverlay = CCLayerColor::create(ccc4(0, 0, 0, 180));
    static_cast<CCLayerColor*>(m_predefPickerOverlay)->setContentSize(winSize);
    m_predefPickerOverlay->setPosition({0, 0});
    m_predefPickerOverlay->setID("predef-picker-overlay"_spr);
    this->addChild(m_predefPickerOverlay, 50);

    float panelW = 380.f;
    float panelH = 220.f;
    float panelX = winSize.width / 2.f - panelW / 2.f;
    float panelY = winSize.height / 2.f - panelH / 2.f;

    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 60);
    panel->setPosition({panelX, panelY});
    m_predefPickerOverlay->addChild(panel);

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.predef.title", "Pick Predefined Tags").c_str(),
        "goldFont.fnt"
    );
    titleLbl->setScale(0.4f);
    titleLbl->setPosition({winSize.width / 2.f, panelY + panelH - 20.f});
    m_predefPickerOverlay->addChild(titleLbl, 1);

    auto hintLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.predef.hint", "Tap to enable/disable").c_str(),
        "bigFont.fnt"
    );
    hintLbl->setScale(0.3f);
    hintLbl->setColor({170, 170, 190});
    hintLbl->setPosition({winSize.width / 2.f, panelY + panelH - 40.f});
    m_predefPickerOverlay->addChild(hintLbl, 1);

    // CCMenu para el boton de cerrar (los CCMenuItemSpriteExtra requieren CCMenu padre)
    auto closeMenu = CCMenu::create();
    closeMenu->setPosition({0, 0});
    closeMenu->setContentSize(winSize);
    closeMenu->setID("predef-picker-close"_spr);
    m_predefPickerOverlay->addChild(closeMenu, 52);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.55f);
    auto closeBtn = CCMenuItemSpriteExtra::create(
        closeSpr, this, menu_selector(PaimonHubLayer::onClosePredefPicker)
    );
    closeBtn->setPosition({panelX + panelW - 12.f, panelY + panelH - 12.f});
    closeMenu->addChild(closeBtn);

    // grid de chips toggleables
    auto chipsMenu = CCMenu::create();
    chipsMenu->setContentSize({panelW - 24.f, panelH - 80.f});
    chipsMenu->setAnchorPoint({0.5f, 0.5f});
    chipsMenu->setPosition({winSize.width / 2.f, panelY + (panelH - 60.f) / 2.f});
    chipsMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_predefPickerOverlay->addChild(chipsMenu, 51);

    for (size_t i = 0; i < m_forumTags.size(); i++) {
        bool isActive = std::find(m_activePredefTags.begin(), m_activePredefTags.end(),
            m_forumTags[i]) != m_activePredefTags.end();

        auto chipSpr = ButtonSprite::create(
            m_forumTags[i].c_str(), "bigFont.fnt",
            isActive ? "GJ_button_01.png" : "GJ_button_05.png", .8f
        );
        chipSpr->setScale(0.34f);
        auto chipBtn = CCMenuItemSpriteExtra::create(
            chipSpr, this, menu_selector(PaimonHubLayer::onTogglePredefTag)
        );
        chipBtn->setTag(static_cast<int>(i));
        chipsMenu->addChild(chipBtn);
    }
    chipsMenu->updateLayout();
}

void PaimonHubLayer::onClosePredefPicker(CCObject*) {
    if (m_predefPickerOverlay) {
        m_predefPickerOverlay->removeFromParent();
        m_predefPickerOverlay = nullptr;
    }
}

void PaimonHubLayer::onTogglePredefTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= (int)m_forumTags.size()) return;

    std::string const& tag = m_forumTags[idx];
    auto it = std::find(m_activePredefTags.begin(), m_activePredefTags.end(), tag);
    if (it != m_activePredefTags.end()) {
        m_activePredefTags.erase(it);
        // si estaba seleccionado para filtrar, des-seleccionar
        auto sel = std::find(m_selectedTags.begin(), m_selectedTags.end(), tag);
        if (sel != m_selectedTags.end()) m_selectedTags.erase(sel);
    } else {
        m_activePredefTags.push_back(tag);
    }

    // actualizar visual del chip
    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            bool isActive = std::find(m_activePredefTags.begin(), m_activePredefTags.end(),
                tag) != m_activePredefTags.end();
            spr->updateBGImage(isActive ? "GJ_button_01.png" : "GJ_button_05.png");
        }
    }

    refreshTagButtons();
}

void PaimonHubLayer::onForumSubTabSwitch(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    switchForumSubTab(idx);
}

void PaimonHubLayer::switchForumSubTab(int idx) {
    m_forumSubTab = idx;

    for (size_t i = 0; i < m_forumSubTabBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_forumSubTabBtns[i]->getNormalImage())) {
            spr->setColor(i == (size_t)idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
        }
    }

    if (m_forumBrowseNode) m_forumBrowseNode->setVisible(idx == 0);
    if (m_forumCreateNode) m_forumCreateNode->setVisible(idx == 1);

    // Swap the shared header so the title matches the active sub-tab.
    if (m_forumHeaderTitle) {
        m_forumHeaderTitle->setString(idx == 0
            ? tr("pai.hub.forum.title", "Community Forum").c_str()
            : tr("pai.hub.forum.create.title", "Create New Post").c_str());
    }
    if (m_forumHeaderSubtitle) {
        m_forumHeaderSubtitle->setString(idx == 0
            ? tr("pai.hub.forum.subtitle",
                 "Share guides, tips and showcases with the community.").c_str()
            : tr("pai.hub.forum.create.subtitle",
                 "Pick a clear title, add details and relevant tags.").c_str());
    }

    if (idx == 0) {
        // Switching back to Browse → reset the inline create form.
        if (m_createTitleInput) m_createTitleInput->setString("");
        if (m_createDescInput) m_createDescInput->setString("");
        m_createSelectedTags.clear();
        if (m_createTagMenu) m_createTagMenu->removeAllChildren();
        refreshForumPosts();
    }
}

void PaimonHubLayer::onCreateToggleTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= (int)m_forumTags.size()) return;
    std::string const& tag = m_forumTags[idx];

    auto it = std::find(m_createSelectedTags.begin(), m_createSelectedTags.end(), tag);
    bool selected = (it != m_createSelectedTags.end());
    if (selected) {
        m_createSelectedTags.erase(it);
    } else {
        m_createSelectedTags.push_back(tag);
    }

    // Visual toggle: GJ_button_01 (gold) when selected, GJ_button_05 (gray)
    // when not. Re-color the chip label so it pops on the gold variant.
    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(selected ? "GJ_button_05.png" : "GJ_button_01.png");
        }
    }

    if (m_createTagsHint) {
        if (m_createSelectedTags.empty()) {
            m_createTagsHint->setString("Tap a tag above to attach it to your post");
            m_createTagsHint->setColor({140, 140, 160});
        } else {
            std::string joined;
            for (size_t i = 0; i < m_createSelectedTags.size(); ++i) {
                if (i > 0) joined += ", ";
                joined += m_createSelectedTags[i];
            }
            m_createTagsHint->setString(("Selected: " + joined).c_str());
            m_createTagsHint->setColor({180, 220, 180});
        }
    }
}

void PaimonHubLayer::onCreateSubmit(CCObject*) {
    std::string title = m_createTitleInput ? std::string(m_createTitleInput->getString()) : std::string();
    std::string desc  = m_createDescInput  ? std::string(m_createDescInput->getString())  : std::string();

    if (title.empty()) {
        PaimonNotify::create("Please enter a title", NotificationIcon::Warning)->show();
        return;
    }

    paimon::forum::CreatePostRequest req;
    req.title = title;
    req.description = desc;
    req.tags = m_createSelectedTags;

    WeakRef<PaimonHubLayer> self = this;
    paimon::forum::ForumApi::get().createPost(req, [self](paimon::forum::Result<paimon::forum::Post> res) {
        auto layer = self.lock();
        if (!layer) return;
        if (!res.ok) {
            PaimonNotify::create(("Failed: " + res.error).c_str(), NotificationIcon::Error)->show();
            return;
        }
        PaimonNotify::create("Post published!", NotificationIcon::Success)->show();
        layer->switchForumSubTab(0);
    });
}

CCMenuItemSpriteExtra* PaimonHubLayer::makeBtn(char const* text, CCPoint pos,
    SEL_MenuHandler handler, CCNode* parent, float scale) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
    btn->setPosition(pos);
    parent->addChild(btn);
    return btn;
}
