#include "PaimonHubLayer.hpp"
#include "PaimonHubData.hpp"
#include "PaiConfigLayer.hpp"
#include "PaimonSupportLayer.hpp"
#include "PaimonModulesLayer.hpp"
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
#include "../features/smooth-scroll/ui/SmoothScrollConfigPopup.hpp"
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
#include "../ui/SmoothUIConfigPopup.hpp"
#include "../ui/HubFeatureInfo.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../utils/InfoButton.hpp"
#include "../utils/Localization.hpp"
#include "../features/guide/services/PaimonGuideService.hpp"
#include "../features/guide/GuideEvents.hpp"
#include "../core/FactoryResetActions.hpp"
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <array>
#include <algorithm>
#include <Geode/binding/GameManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../utils/GeodeTextInputSafe.hpp"
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/General.hpp>

using namespace geode::prelude;

namespace {
std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') return fallback;
    return value;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

void shrinkLabelToFit(CCLabelBMFont* label, float maxW) {
    if (!label) return;
    float w = label->getScaledContentSize().width;
    if (w > maxW) label->setScale(label->getScale() * maxW / w);
}

CCMenu* makeZeroMenu(char const* id = nullptr) {
    auto* menu = CCMenu::create();
    if (id) menu->setID(id);
    menu->setPosition({0.f, 0.f});
    return menu;
}

void dismissOverlay(CCNode*& node) {
    if (node) {
        if (node->getParent()) node->removeFromParent();
        node = nullptr;
    }
}

void animateActionCard(CCMenuItemSpriteExtra* btn, float x, float targetY, float delay) {
    btn->setPosition({x, targetY - 18.f});
    btn->setScale(0.f);
    btn->setOpacity(0);
    btn->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCSpawn::create(
            CCFadeTo::create(0.18f, 255),
            CCEaseBackOut::create(CCMoveTo::create(0.24f, {x, targetY})),
            CCScaleTo::create(0.24f, 1.f),
            nullptr
        ),
        nullptr
    ));
}

CCMenuItemSpriteExtra* makeActionCardBtn(
    paimon::hubdata::HubActionMeta const& action,
    ccColor3B borderColor,
    float cardW, float cardH,
    WeakRef<PaimonHubLayer> self
) {
    auto cardBg = paimon::SpriteHelper::createColorPanel(
        cardW, cardH, {24, 25, 38}, 230, 5.f
    );
    auto accent = paimon::SpriteHelper::createColorPanel(
        cardW - 14.f, 2.f, borderColor, 220, 0.f
    );
    accent->setPosition({7.f, cardH - 4.f});
    cardBg->addChild(accent, 1);

    auto label = CCLabelBMFont::create(action.title.c_str(), "bigFont.fnt");
    label->setAnchorPoint({0.5f, 0.5f});
    constexpr float kLabelScale = 0.38f;
    label->setScale(kLabelScale);
    shrinkLabelToFit(label, cardW - 12.f);
    label->setPosition({cardW / 2.f, cardH / 2.f});
    cardBg->addChild(label);

    return CCMenuItemExt::createSpriteExtra(cardBg, [self, action](CCMenuItemSpriteExtra*) {
        auto selfRef = self.lock();
        auto* hub = selfRef.data();
        if (hub && hub->getParent()) action.onPress(hub);
    });
}

CCScale9Sprite* makeShortcutBg(ccColor3B color = {255, 255, 255}) {
    auto* bg = CCScale9Sprite::create("GJ_button_02.png");
    bg->setContentSize({26.f, 26.f});
    bg->setColor(color);
    return bg;
}

CCLabelBMFont* addCenteredLabel(CCNode* parent, char const* text, float scale,
    ccColor3B color = {255, 255, 255}, char const* id = nullptr)
{
    auto* label = CCLabelBMFont::create(text, "goldFont.fnt");
    label->setScale(scale);
    label->setColor(color);
    label->setPosition(parent->getContentSize() / 2.f);
    if (id) label->setID(id);
    parent->addChild(label);
    return label;
}

template <typename T>
bool contains(std::vector<T> const& v, T const& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

template <typename T>
void eraseOne(std::vector<T>& v, T const& x) {
    auto it = std::find(v.begin(), v.end(), x);
    if (it != v.end()) v.erase(it);
}
} // namespace

namespace paimon::hubdata {

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
                {"Modulos", "GJ_button_03.png", [](PaimonHubLayer*) {
                    auto scene = PaimonModulesLayer::scene();
                    if (scene) CCDirector::get()->pushScene(scene);
                }, 0, "Activa o desactiva funciones"},
                {"Configurar", "GJ_button_01.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(0); }, 0, "Idioma, updates y basicos"},
                {"PaiDraw", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenPaiDraw(nullptr); }, 0, "Dibuja con la comunidad"},
                {"Soporte", "GJ_button_04.png", [](PaimonHubLayer* self) { self->onOpenSupport(nullptr); }, 0, "Ayuda y contacto"},
                {"Reiniciar ajustes", "GJ_button_03.png", [](PaimonHubLayer*) {
                    paimon::factory_reset::requestWithConfirmation();
                }, 0, "Restaura todo por defecto"},
            };
        case 1: // Thumbnails
            return {
                {"Configurar", "GJ_button_02.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(1); }, 1, "Tamano y estilo de celdas"},
                {"Efectos", "GJ_button_03.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(2); }, 1, "Animaciones y transiciones"},
            };
        case 2: // Level
            return {
                {"Configurar", "GJ_button_01.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(3); }, 2, "Fondo y efectos del nivel"},
                {"Barra Progreso", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = ProgressBarConfigPopup::create()) popup->show(); }, 2, "Personaliza la barra"},
            };
        case 3: // Audio
            return {
                {"Configurar", "GJ_button_04.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(4); }, 3, "Musica de menu y capas"},
                {"Musica Perfil", "GJ_button_02.png", [](PaimonHubLayer*) {
                    auto* acc = GJAccountManager::sharedState();
                    int accountID = acc ? acc->m_accountID : 0;
                    if (accountID > 0) {
                        if (auto popup = ProfileMusicPopup::create(accountID)) popup->show();
                    } else {
                        PaimonNotify::create("Necesitas iniciar sesion.", NotificationIcon::Warning)->show();
                    }
                }, 3, "Tu cancion en tu perfil"},
            };
        case 4: // Backgrounds
            return {
                {"Editor Fondos", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenBackgrounds(nullptr); }, 4, "Fondo por pantalla"},
                {"Transiciones", "GJ_button_04.png", [](PaimonHubLayer*) { if (auto popup = TransitionConfigPopup::create()) popup->show(); }, 4, "Animaciones entre escenas"},
                {"Config Full", "GJ_button_01.png", [](PaimonHubLayer* self) { self->onOpenConfig(nullptr); }, 4, "Panel completo de fondos"},
            };
        case 5: // Extras
            return {
                {"Smooth UI", "GJ_button_05.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::ui::SmoothUIConfigPopup::create()) popup->show();
                }, 5, "Animaciones suaves"},
                {"Mascota", "GJ_button_03.png", [](PaimonHubLayer*) { if (auto popup = PetConfigPopup::create()) popup->show(); }, 5, "Companero en pantalla"},
                {"Cursor", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = CursorConfigPopup::create()) popup->show(); }, 5, "Cursor personalizado"},
                {"Slider", "GJ_button_01.png", [](PaimonHubLayer*) { if (auto popup = paimon::slider::CustomSliderPopup::create()) popup->show(); }, 5, "Barra de scroll custom"},
                {"Scroll", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = paimon::smoothscroll::SmoothScrollConfigPopup::create()) popup->show(); }, 5, "Desplazamiento suave"},
                {"Beat Shaders", "GJ_button_04.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::beat_shaders::BeatShaderConfigLayer::create()) {
                        popup->show();
                    }
                }, 5, "Fondos al ritmo"},
                {"Perfil", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenProfiles(nullptr); }, 5, "Editor de foto de perfil"},
                {"Actualizar", "GJ_button_02.png", [](PaimonHubLayer*) {
                    auto& chk = paimon::updates::UpdateChecker::get();
                    auto state = chk.state();

                    if (state == paimon::updates::UpdateChecker::State::UpdateAvailable) {
                        if (auto popup = paimon::updates::UpdateProgressPopup::create()) popup->show();
                        return;
                    }

                    if (chk.hasPendingInstall()) {
                        PopupManager::get().quickPopup(
                            "Actualizar",
                            fmt::format(
                                "La version <cy>{}</c> ya esta descargada.\n<cg>Reiniciar para instalar?</c>",
                                chk.remoteVersion()
                            ),
                            "No", "Reiniciar",
                            [](auto*, bool yes) {
                                if (!yes) return;
                                auto& c = paimon::updates::UpdateChecker::get();
                                if (!c.restartToApplyPendingUpdate()) {
                                    geode::utils::game::restart(true);
                                }
                            }
                        ).showInstant();
                        return;
                    }

                    if (state == paimon::updates::UpdateChecker::State::UpToDate) {
                        PaimonNotify::create(
                            fmt::format("Ya tienes la ultima version ({})", chk.localVersion()),
                            NotificationIcon::Success
                        )->show();
                        return;
                    }

                    if (state == paimon::updates::UpdateChecker::State::Checking) {
                        PaimonNotify::create("Comprobando actualizaciones...", NotificationIcon::Loading)->show();
                        return;
                    }

                    chk.checkAsync();
                    PaimonNotify::create("Buscando actualizaciones... pulsa de nuevo en unos segundos.", NotificationIcon::Loading)->show();
                }, 5, "Busca nueva version"},
            };
        case 6: // Discord
            return {
                {"Configurar", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show(); }, 6, "Rich Presence a tu gusto"},
                {"Refrescar", "GJ_button_05.png", [](PaimonHubLayer*) { paimon::discord::DiscordPresenceManager::get().refreshSoon(); PaimonNotify::create("Rich Presence actualizada.", NotificationIcon::Success)->show(); }, 6, "Fuerza la actualizacion"},
            };
        default:
            return {};
    }
}

std::vector<GranularSettingMeta> getGranularSettings() {
    return {
        {"Language / Idioma", "Idioma / Language", 0},
        {"Auto Update", "Auto Actualizar", 0},
        {"Quick Search Key", "Tecla de Búsqueda Rápida", 0},
        {"Realtime Search Preview", "Vista Previa en Tiempo Real", 0},
        {"Settings Panel Keybind", "Tecla de Panel de Ajustes", 0},
        {"Layout Editor Keybind", "Tecla de Editor de Layout", 0},
        {"Debug Logs", "Registros de Depuración", 0},

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

        {"Background Style (Level)", "Estilo de Fondo de Nivel", 2},
        {"Dynamic Song", "Canción Dinámica", 2},
        {"Progress Bar", "Barra de Progreso", 2},

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

        {"Editor Fondos", "Editor de Fondos", 4},
        {"Transiciones de Fondos", "Transiciones de Fondos", 4},
        {"Configuración Completa", "Configuración Completa", 4},

        {"Smooth UI", "Smooth UI", 5},
        {"Smooth Popups", "Popups Suaves", 5},
        {"Button Animations", "Animaciones de Botones", 5},
        {"Global UI Speed", "Velocidad Global UI", 5},
        {"Reduced Motion", "Reducir Movimiento", 5},
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

        {"Enable Discord Rich Presence", "Activar Discord Rich Presence", 6},
        {"Configure Discord RPC", "Configurar Discord RPC", 6},
        {"Refresh Discord Status", "Refrescar Estado de Discord", 6}
    };
}

} // namespace paimon::hubdata

using namespace paimon::hubdata;

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
    for (auto* n : {m_createPostOverlay, m_createTagOverlay, m_predefPickerOverlay}) {
        if (n && n->getParent()) n->removeFromParent();
    }
}

bool PaimonHubLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    m_forumTags = {
        "Guide", "Tip", "Question", "Bug", "Suggestion",
        "Showcase", "Discussion", "Help", "News", "Update",
        "Level", "Video", "Art", "Music", "Story",
        "Theory", "Challenge", "Competition", "Feedback", "Other"
    };

    m_gdMode = Mod::get()->getSavedValue<std::string>("hub-ui-style", "original") == "gd";
    if (m_gdMode) {
        buildGDShell();
        return true;
    }

    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2;
    float top = winSize.height;

    if (auto bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX(winSize.width / bg->getContentSize().width);
        bg->setScaleY(winSize.height / bg->getContentSize().height);
        bg->setColor({20, 20, 32});
        this->addChild(bg, -2);
        m_bgNode = bg;
    } else {
        auto flat = CCLayerColor::create(ccc4(20, 20, 32, 255));
        flat->setContentSize(winSize);
        this->addChild(flat, -2);
        m_bgNode = flat;
    }
    m_bgColorHome = {20, 20, 32};
    m_bgColorSub  = {0, 102, 255};

    m_mainMenu = makeZeroMenu("paimon-hub-main-menu"_spr);
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

    auto uiSpr = ButtonSprite::create(tr("pai.hub.style.gd", "UI: GD").c_str(), "bigFont.fnt", "GJ_button_05.png", .8f);
    uiSpr->setScale(0.34f);
    auto uiBtn = CCMenuItemSpriteExtra::create(uiSpr, this, menu_selector(PaimonHubLayer::onToggleUIStyle));
    uiBtn->setID("ui-style-btn"_spr);
    uiBtn->setPosition({winSize.width - 58.f, top - 20.f});
    m_mainMenu->addChild(uiBtn);

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

    static char const* kTabIds[] = {"home-tab-btn"_spr, "news-tab-btn"_spr, "forum-tab-btn"_spr};
    for (int i = 0; i < 3; i++) {
        auto spr = ButtonSprite::create(tabNames[i].c_str(), "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(0.38f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonHubLayer::onTabSwitch));
        btn->setTag(i);
        btn->setID(kTabIds[i]);
        tabBar->addChild(btn);
        m_tabBtns.push_back(btn);
    }
    tabBar->updateLayout();

    auto sep = CCLayerColor::create({100, 150, 255, 60});
    sep->setContentSize({winSize.width - 30, 1});
    sep->setPosition({15, top - 38.f});
    this->addChild(sep, 5);

    auto addTab = [this](CCLayerRGBA*& tab, CCMenu*& menu, char const* tabId, char const* menuId, bool visible) {
        tab = cocos2d::CCLayerRGBA::create();
        tab->setID(tabId);
        tab->setCascadeOpacityEnabled(true);
        tab->setVisible(visible);
        this->addChild(tab, 5);
        menu = makeZeroMenu(menuId);
        menu->setVisible(visible);
        this->addChild(menu, 11);
    };
    addTab(m_homeTab, m_homeMenu, "home-tab"_spr, "home-menu"_spr, true);
    addTab(m_newsTab, m_newsMenu, "news-tab"_spr, "news-menu"_spr, false);
    addTab(m_forumTab, m_forumMenu, "forum-tab"_spr, "forum-menu"_spr, false);

    buildHomeTab();
    buildNewsTab();
    buildForumTab();
    switchTab(0);
    return true;
}

void PaimonHubLayer::keyBackClicked() {
    if (m_gdMode) {
        if (m_gdTourOverlay) { gdEndTour(); return; }
        if (m_currentTab == 0 && m_gdHomeState != 0) {
            gdShowCategories();
            return;
        }
    }
    // MenuLayer::scene(false) full setup — manual scene left black on Escape
    CCDirector::get()->replaceScene(MenuLayer::scene(false));
}

void PaimonHubLayer::onToggleUIStyle(CCObject*) {
    bool toGD = !m_gdMode;
    Mod::get()->setSavedValue<std::string>("hub-ui-style", toGD ? "gd" : "original");
    auto scene = CCScene::create();
    scene->addChild(PaimonHubLayer::create());
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.4f, scene));
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
    int prev = m_currentTab;
    m_currentTab = idx;

    m_homeTab->setVisible(idx == 0);
    m_homeMenu->setVisible(idx == 0);
    m_newsTab->setVisible(idx == 1);
    m_newsMenu->setVisible(idx == 1);
    m_forumTab->setVisible(idx == 2);
    m_forumMenu->setVisible(idx == 2);

    cocos2d::CCLayerRGBA* activeTab = (idx == 0) ? m_homeTab : (idx == 1) ? m_newsTab : m_forumTab;
    cocos2d::CCMenu* activeMenu = (idx == 0) ? m_homeMenu : (idx == 1) ? m_newsMenu : m_forumMenu;
    float dx = (idx > prev) ? 46.f : (idx < prev) ? -46.f : 0.f;

    if (activeTab) {
        activeTab->stopAllActions();
        activeTab->setOpacity(0);
        activeTab->setPosition({dx, 0.f});
        activeTab->runAction(CCSpawn::create(
            CCFadeTo::create(0.20f, 255),
            CCEaseExponentialOut::create(CCMoveTo::create(0.32f, {0.f, 0.f})),
            nullptr
        ));
    }
    if (activeMenu) {
        activeMenu->stopAllActions();
        activeMenu->setPosition({dx, 0.f});
        activeMenu->runAction(CCEaseExponentialOut::create(CCMoveTo::create(0.32f, {0.f, 0.f})));
    }

    if (m_bgNode) {
        auto target = (idx == 0) ? m_bgColorHome : m_bgColorSub;
        m_bgNode->stopAllActions();
        m_bgNode->runAction(CCTintTo::create(0.32f, target.r, target.g, target.b));
    }

    for (int i = 0; i < (int)m_tabBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_tabBtns[i]->getNormalImage())) {
            spr->setColor(i == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
        }
    }
    if (idx == 2) refreshForumPosts();
}

void PaimonHubLayer::buildHomeTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;

    auto categories = getHubCategories();

    m_sidebarBg = paimon::SpriteHelper::createDarkPanel(135.f, 250.f, 220, 6.f);
    m_sidebarBg->setPosition({15.f, 15.f});
    m_homeTab->addChild(m_sidebarBg, 0);

    m_sidebarHighlight = paimon::SpriteHelper::createColorPanel(
        125.f, 26.f, cocos2d::ccColor3B{255, 255, 255}, 200, 4.f
    );
    m_sidebarHighlight->setPosition({20.f, 241.f - 13.f});
    m_homeTab->addChild(m_sidebarHighlight, 1);
    if (m_sidebarHighlight && !categories.empty()) {
        m_sidebarHighlight->setColor(categories[0].color);
    }

    m_sidebarMenu = makeZeroMenu("paimon-sidebar-menu"_spr);
    m_homeTab->addChild(m_sidebarMenu, 2);
    m_sidebarLabels.clear();

    for (size_t i = 0; i < categories.size(); i++) {
        float yPos = 241.f - i * 27.f;
        auto label = CCLabelBMFont::create(categories[i].title.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.5f, 0.5f});
        label->setPosition({20.f + 125.f / 2.f, yPos});
        label->setScale(0.28f);
        m_homeTab->addChild(label, 3);
        m_sidebarLabels.push_back(label);

        auto btnBg = CCNode::create();
        btnBg->setContentSize({125.f, 26.f});
        auto btn = CCMenuItemSpriteExtra::create(btnBg, this, menu_selector(PaimonHubLayer::onTabSwitch));
        btn->setTag(100 + static_cast<int>(i));
        btn->setPosition({20.f + 125.f / 2.f, yPos});
        m_sidebarMenu->addChild(btn);
    }

    {
        auto* shortcutsRow = CCMenu::create();
        shortcutsRow->setID("hub-shortcuts-row"_spr);
        shortcutsRow->setContentSize({135.f, 30.f});
        shortcutsRow->setAnchorPoint({0.5f, 0.5f});
        shortcutsRow->ignoreAnchorPointForPosition(false);
        shortcutsRow->setPosition({135.f * 0.5f + 5.f + 10.f, 32.f});
        shortcutsRow->setLayout(
            RowLayout::create()
                ->setGap(6.f)
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
        );

        auto* discordBg = makeShortcutBg({110, 150, 255});
        addCenteredLabel(discordBg, "RPC", 0.26f);
        auto* discordBtn = CCMenuItemExt::createSpriteExtra(discordBg, [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
            if (auto* hub = self.lock().data(); hub && hub->getParent()) hub->onOpenDiscordConfig(nullptr);
        });
        discordBtn->setID("discord-sidebar-btn"_spr);
        shortcutsRow->addChild(discordBtn);

        auto* qhBg = makeShortcutBg({255, 200, 80});
        addCenteredLabel(qhBg, "QH", 0.26f);
        auto* qhBtn = CCMenuItemExt::createSpriteExtra(qhBg, [](CCMenuItemSpriteExtra*) {
            if (auto popup = paimon::quickhub::RadialConfigPopup::create()) popup->show();
        });
        qhBtn->setID("quickhub-sidebar-btn"_spr);
        shortcutsRow->addChild(qhBtn);

        auto* drawBg = makeShortcutBg();
        if (auto* icon = paidraw::createPaiDrawIcon(20.f)) {
            icon->setPosition(drawBg->getContentSize() / 2.f);
            drawBg->addChild(icon);
        }
        auto* drawBtn = CCMenuItemSpriteExtra::create(drawBg, this, menu_selector(PaimonHubLayer::onOpenPaiDraw));
        drawBtn->setID("paidraw-sidebar-btn"_spr);
        shortcutsRow->addChild(drawBtn);

        bool guideOn = paimon::guide::PaimonGuideService::get().isEnabled();
        auto* guideBg = makeShortcutBg(guideOn ? ccColor3B{255, 220, 100} : ccColor3B{120, 120, 130});
        addCenteredLabel(guideBg, "GUI", 0.26f,
            guideOn ? ccColor3B{255, 255, 255} : ccColor3B{180, 180, 180},
            "guide-sidebar-btn-label"_spr);

        auto* guideBtn = CCMenuItemExt::createSpriteExtra(guideBg, [](CCMenuItemSpriteExtra* sender) {
            using namespace paimon::guide;
            bool newState = !PaimonGuideService::get().isEnabled();
            PaimonGuideService::get().setEnabled(newState);
            GuideEnabledChangedEvent(kGuideEventFilter).send(newState);

            if (auto* nine = typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(
                    sender ? sender->getNormalImage() : nullptr)) {
                nine->setColor(newState ? ccColor3B{255, 220, 100} : ccColor3B{120, 120, 130});
                if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(
                        nine->getChildByIDRecursive("guide-sidebar-btn-label"_spr))) {
                    lbl->setColor(newState ? ccColor3B{255, 255, 255} : ccColor3B{180, 180, 180});
                }
            }
            Notification::create(
                Localization::get().getString(newState ? "pai.guide.toggle.on" : "pai.guide.toggle.off"),
                newState ? NotificationIcon::Success : NotificationIcon::None,
                1.8f
            )->show();
        });
        guideBtn->setID("guide-sidebar-btn"_spr);
        shortcutsRow->addChild(guideBtn);
        shortcutsRow->updateLayout();
        m_sidebarMenu->addChild(shortcutsRow);
    }

    float detailsW = winSize.width - 175.f;
    m_detailsBg = paimon::SpriteHelper::createDarkPanel(detailsW, 250.f, 240, 6.f);
    m_detailsBg->setPosition({160.f, 15.f});
    m_homeTab->addChild(m_detailsBg, 0);

    m_homeCategoryTitle = CCLabelBMFont::create(categories[0].title.c_str(), "goldFont.fnt");
    m_homeCategoryTitle->setScale(0.48f);
    m_homeCategoryTitle->setColor(categories[0].color);
    m_homeCategoryTitle->setAnchorPoint({0.f, 0.5f});
    m_homeCategoryTitle->setPosition({178.f, 242.f});
    m_homeTab->addChild(m_homeCategoryTitle, 2);

    m_homeCategoryInfoBtn = CCMenuItemExt::createSpriteExtra(
        CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"),
        [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
            auto selfRef = self.lock();
            auto* hub = selfRef.data();
            if (!hub || !hub->getParent()) return;
            auto categories = getHubCategories();
            if (hub->m_homeSelectedCategory < 0 || hub->m_homeSelectedCategory >= static_cast<int>(categories.size())) return;
            auto const& cat = categories[hub->m_homeSelectedCategory];
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

    m_searchInput = TextInput::create(120.f, tr("pai.hub.search", "Search...").c_str(), "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setMaxCharCount(20);
    m_searchInput->setPosition({winSize.width - 95.f, 236.f});
    m_searchInput->setScale(0.68f);
    WeakRef<PaimonHubLayer> hubRef = this;
    m_searchInput->setCallback([hubRef](std::string const&) {
        auto selfRef = hubRef.lock();
        auto* hub = selfRef.data();
        if (hub && hub->getParent()) {
            hub->rebuildHomeCategoryCards();
        }
    });
    m_homeTab->addChild(m_searchInput, 10);

    m_homeActionsAnchor = CCNode::create();
    m_homeTab->addChild(m_homeActionsAnchor, 2);

    m_homeActionsMenu = makeZeroMenu();
    m_homeMenu->addChild(m_homeActionsMenu, 3);

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

    if (m_searchInput) m_searchInput->setString("");
    if (m_homeActionsScroll) {
        m_homeActionsScroll->removeFromParent();
        m_homeActionsScroll = nullptr;
    }
    if (m_homeActionsMenu) m_homeActionsMenu->setVisible(true);

    refreshHomeCategorySelector();
    if (m_homeCategoryTitle) {
        m_homeCategoryTitle->setString(categories[idx].title.c_str());
        m_homeCategoryTitle->setColor(categories[idx].color);
    }
    if (m_homeCategoryDesc) m_homeCategoryDesc->setString(categories[idx].shortDesc.c_str());
    rebuildHomeCategoryCards();
}

void PaimonHubLayer::refreshHomeCategorySelector() {
    auto categories = getHubCategories();
    if (m_homeSelectedCategory < 0 || m_homeSelectedCategory >= static_cast<int>(categories.size())) return;

    float targetY = 241.f - m_homeSelectedCategory * 27.f;
    if (m_sidebarHighlight) {
        auto c = categories[m_homeSelectedCategory].color;
        m_sidebarHighlight->stopAllActions();
        m_sidebarHighlight->runAction(CCEaseExponentialOut::create(
            CCMoveTo::create(0.22f, {20.f, targetY - 13.f})
        ));
        m_sidebarHighlight->runAction(CCTintTo::create(0.22f, c.r, c.g, c.b));
    }

    for (size_t i = 0; i < m_sidebarLabels.size(); i++) {
        if (!m_sidebarLabels[i]) continue;
        bool sel = static_cast<int>(i) == m_homeSelectedCategory;
        m_sidebarLabels[i]->setColor(sel ? ccColor3B{255, 255, 255} : ccColor3B{130, 140, 165});
        m_sidebarLabels[i]->runAction(CCScaleTo::create(0.12f, sel ? 0.32f : 0.28f));
    }

    if (m_homeCategoryInfoBtn && m_homeCategoryTitle) {
        m_homeCategoryInfoBtn->setPosition({
            178.f + m_homeCategoryTitle->getScaledContentSize().width + 12.f, 242.f
        });
    }
}

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
        "<cy>{}</c> = layout menu/pausa\n"
        "(en Geode)\n\n"
        "Arrastra | verde=tamano\n"
        "azul=opacidad | rojo=ocultar\n"
        "<cy>Esc</c> = salir\n\n"
        "Hub: izq. categorias,\n"
        "abajo acciones.",
        layoutKeybind
    );

    PopupManager::get().alert("Ayuda", body).showInstant();
}

void PaimonHubLayer::rebuildHomeCategoryCards() {
    if (!m_homeActionsMenu) return;
    m_homeActionsMenu->removeAllChildren();
    if (m_homeActionsScroll) {
        m_homeActionsScroll->removeFromParent();
        m_homeActionsScroll = nullptr;
    }
    m_homeActionsMenu->setVisible(true);

    auto categories = getHubCategories();
    if (m_homeSelectedCategory < 0 || m_homeSelectedCategory >= static_cast<int>(categories.size())) return;
    auto const& cat = categories[m_homeSelectedCategory];

    std::vector<HubActionMeta> actions;
    std::string query = m_searchInput ? m_searchInput->getString() : "";

    if (query.empty()) {
        actions = getHubActions(m_homeSelectedCategory);
        for (auto& act : actions) act.categoryIndex = m_homeSelectedCategory;
        if (m_homeCategoryTitle) {
            m_homeCategoryTitle->setString(cat.title.c_str());
            m_homeCategoryTitle->setColor(cat.color);
        }
        if (m_homeCategoryDesc) m_homeCategoryDesc->setString(cat.shortDesc.c_str());
        if (m_homeCategoryInfoBtn) {
            m_homeCategoryInfoBtn->setVisible(true);
            m_homeCategoryInfoBtn->setPosition({
                178.f + m_homeCategoryTitle->getScaledContentSize().width + 12.f, 242.f
            });
        }
    } else {
        std::string lowerQuery = toLower(query);
        for (size_t catIdx = 0; catIdx < categories.size(); ++catIdx) {
            for (auto const& act : getHubActions(static_cast<int>(catIdx))) {
                if (toLower(act.title).find(lowerQuery) != std::string::npos ||
                    toLower(categories[catIdx].title).find(lowerQuery) != std::string::npos) {
                    HubActionMeta searchAct = act;
                    searchAct.title = categories[catIdx].title + ": " + act.title;
                    searchAct.categoryIndex = static_cast<int>(catIdx);
                    actions.push_back(std::move(searchAct));
                }
            }
        }

        bool isSpanish = geode::Mod::get()->getSettingValue<std::string>("language") == "spanish";
        for (auto const& gs : getGranularSettings()) {
            if (toLower(gs.englishName).find(lowerQuery) == std::string::npos &&
                toLower(gs.spanishName).find(lowerQuery) == std::string::npos) continue;
            HubActionMeta searchAct;
            searchAct.title = isSpanish ? gs.spanishName : gs.englishName;
            searchAct.sprite = "GJ_button_02.png";
            searchAct.categoryIndex = gs.categoryIndex;
            searchAct.onPress = [gs](PaimonHubLayer*) {
                paimon::ui::openFeatureConfigFor(gs.englishName, gs.categoryIndex);
            };
            actions.push_back(std::move(searchAct));
        }

        if (m_homeCategoryTitle) {
            m_homeCategoryTitle->setString(tr("pai.hub.search_results", "Search Results").c_str());
            m_homeCategoryTitle->setColor({255, 255, 255});
        }
        if (m_homeCategoryDesc) {
            m_homeCategoryDesc->setString(fmt::format(
                fmt::runtime(tr("pai.hub.search_found", "Found {} features matching search.")),
                actions.size()
            ).c_str());
        }
        if (m_homeCategoryInfoBtn) m_homeCategoryInfoBtn->setVisible(false);
    }

    auto winSize = CCDirector::get()->getWinSize();
    float detailsW = winSize.width - 175.f;
    float rightPanelCX = 160.f + detailsW / 2.f;
    size_t n = actions.size();
    if (n == 0) return;

    constexpr float kCardW = 112.f, kCardH = 48.f;
    WeakRef<PaimonHubLayer> self = this;

    if (n > 4) {
        m_homeActionsMenu->setVisible(false);
        float scrollW = detailsW - 20.f;
        float scrollH = 175.f;
        m_homeActionsScroll = geode::ScrollLayer::create(CCSize{scrollW, scrollH});
        m_homeActionsScroll->setPosition({160.f + 10.f, 20.f});
        m_homeActionsScroll->setID("search-actions-scroll"_spr);
        m_homeTab->addChild(m_homeActionsScroll, 3);

        constexpr float colWidth = 120.f, rowHeight = 56.f;
        int numCols = std::max(1, static_cast<int>(scrollW / colWidth));
        int numRows = (static_cast<int>(n) + numCols - 1) / numCols;
        float contentHeight = std::max(scrollH, static_cast<float>(numRows) * rowHeight + 10.f);
        m_homeActionsScroll->m_contentLayer->setContentSize(CCSize{scrollW, contentHeight});

        auto scrollMenu = makeZeroMenu();
        scrollMenu->setContentSize(m_homeActionsScroll->m_contentLayer->getContentSize());
        m_homeActionsScroll->m_contentLayer->addChild(scrollMenu, 10);

        float startX = (scrollW - static_cast<float>(numCols) * colWidth) / 2.f;
        for (size_t i = 0; i < n; ++i) {
            auto const& action = actions[i];
            int row = static_cast<int>(i / numCols);
            int col = static_cast<int>(i % numCols);
            float x = startX + col * colWidth + colWidth / 2.f;
            float targetY = contentHeight - (row * rowHeight + rowHeight / 2.f + 5.f);
            auto btn = makeActionCardBtn(action, categories[action.categoryIndex].color, kCardW, kCardH, self);
            scrollMenu->addChild(btn);
            animateActionCard(btn, x, targetY, 0.02f * static_cast<float>(i));
        }
    } else {
        m_homeActionsMenu->setVisible(true);
        for (size_t i = 0; i < n; ++i) {
            auto const& action = actions[i];
            float x = rightPanelCX, targetY = 110.f;
            if (n <= 3) {
                float spacing = 125.f;
                x = rightPanelCX - spacing * static_cast<float>(n - 1) / 2.f + static_cast<float>(i) * spacing;
            } else {
                x = rightPanelCX + (i % 2 == 0 ? -65.f : 65.f);
                targetY = (i < 2) ? 142.f : 82.f;
            }
            auto btn = makeActionCardBtn(action, categories[action.categoryIndex].color, kCardW, kCardH, self);
            m_homeActionsMenu->addChild(btn);
            animateActionCard(btn, x, targetY, 0.04f * static_cast<float>(i));
        }
    }
}

namespace {
    struct NewsItem {
        std::string title;
        std::string desc;
        bool highlight = false;
    };

    std::vector<NewsItem> buildNewsItems() {
        std::vector<NewsItem> items;

        auto& chk = paimon::updates::UpdateChecker::get();
        if (chk.state() == paimon::updates::UpdateChecker::State::UpdateAvailable) {
            items.push_back({
                fmt::format(
                    fmt::runtime(tr("pai.hub.news.update.title", "Update {} available!")),
                    chk.remoteVersion()
                ),
                tr("pai.hub.news.update.desc", "Go to Extras > Update to install it."),
                true
            });
        } else {
            items.push_back({
                fmt::format(
                    fmt::runtime(tr("pai.hub.news.version.title", "Version {} installed")),
                    chk.localVersion()
                ),
                tr("pai.hub.news.version.desc", "Your current version of Paimbnails."),
                false
            });
        }

        items.push_back({
            tr("pai.hub.news.item1.title", "Welcome to Paimon Hub!"),
            tr("pai.hub.news.item1.desc", "Check out our new hub with news and forum sections.")
        });
        items.push_back({
            tr("pai.hub.news.item3.title", "Custom Profiles"),
            tr("pai.hub.news.item3.desc", "Create and share your custom profile pictures.")
        });
        return items;
    }

    constexpr cocos2d::ccColor4B kListRowDark  = {161, 88, 44, 255};
    constexpr cocos2d::ccColor4B kListRowLight = {194, 114, 62, 255};
    constexpr cocos2d::ccColor3B kListTextSoft = {255, 235, 190};

    cocos2d::CCNode* makeGDPanel(cocos2d::CCNode* parent, float panelW, float panelH) {
        if (auto panel = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
            panel->setAnchorPoint({0.f, 0.f});
            panel->setContentSize({panelW, panelH});
            panel->setPosition({15.f, 15.f});
            parent->addChild(panel, 0);
            return panel;
        }
        auto fallback = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 245, 6.f);
        fallback->setPosition({15.f, 15.f});
        parent->addChild(fallback, 0);
        return fallback;
    }

    void addGDListChrome(
        cocos2d::CCNode* parent,
        float centerX, float centerY,
        float listW, float listH
    ) {
        if (auto inset = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
            inset->setContentSize({listW + 8.f, listH + 8.f});
            inset->setColor({0, 0, 0});
            inset->setOpacity(90);
            inset->setPosition({centerX, centerY});
            parent->addChild(inset, 1);
        }
        if (auto borders = geode::ListBorders::create()) {
            borders->setContentSize({listW + 6.f, listH});
            borders->setPosition({centerX, centerY});
            parent->addChild(borders, 6);
        }
    }

    cocos2d::CCNode* makeGDRefreshSprite() {
        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png")) {
            spr->setScale(0.72f);
            return spr;
        }
        auto fallback = ButtonSprite::create("R", "goldFont.fnt", "GJ_button_02.png", .8f);
        fallback->setScale(0.5f);
        return fallback;
    }
} // namespace

void PaimonHubLayer::buildNewsTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;
    float panelW = winSize.width - 30.f;
    float panelH = 250.f;

    auto panel = makeGDPanel(m_newsTab, panelW, panelH);
    panel->setID("news-panel"_spr);

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.news.title", "Latest News").c_str(), "goldFont.fnt"
    );
    titleLbl->setScale(0.55f);
    titleLbl->setPosition({cx, 245.f});
    m_newsTab->addChild(titleLbl, 2);

    if (auto emote = paimon::SpriteHelper::safeCreate("paim_Paimon.png"_spr)) {
        float h = emote->getContentSize().height;
        if (h > 1.f) emote->setScale(26.f / h);
        emote->setPosition({cx - titleLbl->getScaledContentSize().width / 2.f - 20.f, 245.f});
        m_newsTab->addChild(emote, 2);
    }

    float listW = panelW - 36.f;
    float listH = 190.f;
    float listX = 33.f;
    float listY = 28.f;

    addGDListChrome(m_newsTab, cx, listY + listH / 2.f, listW, listH);

    m_newsScroll = ScrollLayer::create({listW, listH});
    m_newsScroll->setPosition({listX, listY});
    m_newsScroll->setID("news-scroll"_spr);
    m_newsTab->addChild(m_newsScroll, 2);

    auto refreshBtn = CCMenuItemSpriteExtra::create(
        makeGDRefreshSprite(), this, menu_selector(PaimonHubLayer::onRefreshNews)
    );
    refreshBtn->setID("news-refresh"_spr);
    refreshBtn->setPosition({winSize.width - 18.f, 18.f});
    m_newsMenu->addChild(refreshBtn);

    rebuildNewsList();
}

void PaimonHubLayer::rebuildNewsList() {
    if (!m_newsScroll) return;
    auto content = m_newsScroll->m_contentLayer;
    content->removeAllChildren();

    auto items = buildNewsItems();

    float listW = m_newsScroll->getContentSize().width;
    float listH = m_newsScroll->getContentSize().height;
    constexpr float kRowH = 50.f;
    float totalH = static_cast<float>(items.size()) * kRowH;
    if (totalH < listH) totalH = listH;
    content->setContentSize({listW, totalH});

    float y = totalH;
    for (size_t i = 0; i < items.size(); i++) {
        y -= kRowH;

        auto row = cocos2d::CCNodeRGBA::create();
        row->setContentSize({listW, kRowH});
        row->setAnchorPoint({0.f, 0.f});
        row->setPosition({0.f, y});
        row->setCascadeOpacityEnabled(true);
        content->addChild(row, 1);

        auto rowBg = CCLayerColor::create(i % 2 == 0 ? kListRowLight : kListRowDark);
        rowBg->setContentSize({listW, kRowH});
        rowBg->setPosition({0.f, 0.f});
        row->addChild(rowBg, 0);

        if (i > 0) {
            auto line = CCLayerColor::create({0, 0, 0, 60});
            line->setContentSize({listW, 1.f});
            line->setPosition({0.f, kRowH - 1.f});
            row->addChild(line, 2);
        }

        auto itemTitle = CCLabelBMFont::create(items[i].title.c_str(), "goldFont.fnt");
        itemTitle->setScale(0.36f);
        itemTitle->setAnchorPoint({0.f, 0.5f});
        itemTitle->setPosition({14.f, kRowH - 14.f});
        shrinkLabelToFit(itemTitle, listW - (items[i].highlight ? 90.f : 28.f));
        row->addChild(itemTitle, 1);

        auto itemDesc = CCLabelBMFont::create(items[i].desc.c_str(), "chatFont.fnt");
        itemDesc->setScale(0.50f);
        itemDesc->setColor({255, 250, 240});
        itemDesc->setAnchorPoint({0.f, 0.5f});
        itemDesc->setPosition({14.f, 15.f});
        shrinkLabelToFit(itemDesc, listW - 28.f);
        row->addChild(itemDesc, 1);

        if (items[i].highlight) {
            auto newBadge = CCLabelBMFont::create("NEW", "bigFont.fnt");
            newBadge->setScale(0.28f);
            newBadge->setColor({100, 255, 100});
            newBadge->setAnchorPoint({1.f, 0.5f});
            newBadge->setPosition({listW - 12.f, kRowH - 14.f});
            row->addChild(newBadge, 1);
            newBadge->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.6f, 140),
                CCFadeTo::create(0.6f, 255),
                nullptr
            )));
        }

        row->setOpacity(0);
        row->runAction(CCSequence::create(
            CCDelayTime::create(0.04f * static_cast<float>(i)),
            CCFadeTo::create(0.2f, 255),
            nullptr
        ));
    }
    m_newsScroll->scrollToTop();
}

namespace {
    constexpr float kForumHeaderY     = 246.f;
    constexpr float kForumSubtitleY   = 230.f;
    constexpr float kForumToolbarY    = 213.f;
    constexpr float kForumChipsY      = 196.f;
    constexpr float kForumListTop     = 186.f;
    constexpr float kForumListBottom  = 26.f;

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
    float contentLeft = 33.f;
    float contentRight = winSize.width - 33.f;

    auto panel = makeGDPanel(m_forumTab, panelW, panelH);
    panel->setID("forum-panel"_spr);

    m_forumSubTabBtns.clear();

    m_forumHeaderTitle = CCLabelBMFont::create(
        tr("pai.hub.forum.title", "Community Forum").c_str(),
        "goldFont.fnt"
    );
    m_forumHeaderTitle->setScale(0.5f);
    m_forumHeaderTitle->setPosition({cx, kForumHeaderY});
    m_forumTab->addChild(m_forumHeaderTitle, 2);

    if (auto emote = paimon::SpriteHelper::safeCreate("paim_Paimon.png"_spr)) {
        float h = emote->getContentSize().height;
        if (h > 1.f) emote->setScale(24.f / h);
        emote->setPosition({cx - 118.f, kForumHeaderY});
        m_forumTab->addChild(emote, 2);
    }

    m_forumHeaderSubtitle = CCLabelBMFont::create(
        tr("pai.hub.forum.subtitle",
            "Share guides, tips and showcases with the community.").c_str(),
        "bigFont.fnt"
    );
    m_forumHeaderSubtitle->setScale(0.22f);
    m_forumHeaderSubtitle->setColor(kListTextSoft);
    m_forumHeaderSubtitle->setPosition({cx, kForumSubtitleY});
    m_forumTab->addChild(m_forumHeaderSubtitle, 2);

    m_forumBrowseNode = CCNode::create();
    m_forumBrowseNode->setPosition({0, 0});
    m_forumBrowseNode->setContentSize(winSize);
    m_forumTab->addChild(m_forumBrowseNode, 1);

    auto browseMenu = makeZeroMenu("forum-browse-menu"_spr);
    browseMenu->setContentSize(winSize);
    m_forumBrowseNode->addChild(browseMenu, 2);

    {
        auto sortLabel = CCLabelBMFont::create(
            tr("pai.hub.forum.sort", "Sort:").c_str(), "bigFont.fnt"
        );
        sortLabel->setScale(0.30f);
        sortLabel->setColor(kListTextSoft);
        sortLabel->setAnchorPoint({0.f, 0.5f});
        sortLabel->setPosition({contentLeft, kForumToolbarY});
        m_forumBrowseNode->addChild(sortLabel, 1);

        auto sortMenu = CCMenu::create();
        sortMenu->setID("forum-sort"_spr);
        sortMenu->setContentSize({170.f, 24.f});
        sortMenu->setAnchorPoint({0.f, 0.5f});
        sortMenu->setPosition(
            {contentLeft + sortLabel->getScaledContentSize().width + 8.f, kForumToolbarY}
        );
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
                    : ccColor3B{255, 255, 255});
            }
        }

        auto tagMenuBar = CCMenu::create();
        tagMenuBar->setID("forum-tag-toolbar"_spr);
        tagMenuBar->setContentSize({190.f, 24.f});
        tagMenuBar->setAnchorPoint({1.f, 0.5f});
        tagMenuBar->setPosition({contentRight, kForumToolbarY});
        tagMenuBar->setLayout(
            RowLayout::create()
                ->setGap(5.f)
                ->setAutoScale(false)
                ->setAxisAlignment(AxisAlignment::End)
        );
        m_forumBrowseNode->addChild(tagMenuBar, 1);

        auto tagLabel = CCLabelBMFont::create(
            tr("pai.hub.forum.tags", "Tags:").c_str(), "bigFont.fnt"
        );
        tagLabel->setScale(0.30f);
        tagLabel->setColor(kListTextSoft);
        tagMenuBar->addChild(tagLabel);

        tagMenuBar->addChild(makeForumPill(
            tr("pai.hub.forum.predef", "Predef").c_str(),
            "GJ_button_05.png", 0.32f,
            menu_selector(PaimonHubLayer::onOpenPredefPicker), this
        ));
        tagMenuBar->addChild(makeForumPill(
            "+", "GJ_button_06.png", 0.42f,
            menu_selector(PaimonHubLayer::onCreateTag), this
        ));
        tagMenuBar->updateLayout();

        auto newPostSpr = ButtonSprite::create(
            tr("pai.hub.forum.create.cta", "+ New Post").c_str(),
            "goldFont.fnt", "GJ_button_01.png", .8f
        );
        newPostSpr->setScale(0.42f);
        auto newPostBtn = CCMenuItemSpriteExtra::create(
            newPostSpr, this, menu_selector(PaimonHubLayer::onForumSubTabSwitch)
        );
        newPostBtn->setTag(1);
        newPostBtn->setID("forum-new-post"_spr);
        newPostBtn->setPosition({
            contentRight - newPostSpr->getScaledContentSize().width / 2.f,
            kForumHeaderY
        });
        browseMenu->addChild(newPostBtn);

        auto refreshCircle = CCMenuItemExt::createSpriteExtra(
            makeGDRefreshSprite(),
            [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
                auto selfRef = self.lock();
                auto* hub = selfRef.data();
                if (hub && hub->getParent()) hub->refreshForumPosts();
            }
        );
        refreshCircle->setID("forum-refresh"_spr);
        refreshCircle->setPosition({winSize.width - 18.f, 18.f});
        browseMenu->addChild(refreshCircle);
    }

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
    m_emptyTagsHint->setScale(0.24f);
    m_emptyTagsHint->setColor(kListTextSoft);
    m_emptyTagsHint->setOpacity(180);
    m_emptyTagsHint->setPosition({cx, kForumChipsY});
    m_forumBrowseNode->addChild(m_emptyTagsHint, 1);

    refreshTagButtons();

    float listW = panelW - 36.f;
    float listH = kForumListTop - kForumListBottom;

    addGDListChrome(m_forumBrowseNode, cx, kForumListBottom + listH / 2.f, listW, listH);

    m_noPostsLabel = CCLabelBMFont::create(
        tr("pai.hub.forum.no_posts",
           "No posts here yet — tap +New Post to start the conversation.").c_str(),
        "bigFont.fnt"
    );
    m_noPostsLabel->setScale(0.30f);
    m_noPostsLabel->setColor(kListTextSoft);
    shrinkLabelToFit(m_noPostsLabel, listW - 20.f);
    m_noPostsLabel->setPosition({cx, kForumListBottom + listH / 2.f});
    m_forumBrowseNode->addChild(m_noPostsLabel, 3);

    m_forumPostList = CCNode::create();
    m_forumPostList->setPosition({cx, kForumListTop});
    m_forumPostList->setContentSize({listW, listH});
    m_forumBrowseNode->addChild(m_forumPostList, 2);

    refreshForumPosts();

    m_forumCreateNode = CCNode::create();
    m_forumCreateNode->setPosition({0, 0});
    m_forumCreateNode->setContentSize(winSize);
    m_forumCreateNode->setVisible(false);
    m_forumTab->addChild(m_forumCreateNode, 1);

    auto lblTitle = CCLabelBMFont::create(
        tr("pai.hub.forum.title_placeholder", "Title").c_str(), "goldFont.fnt");
    lblTitle->setScale(0.35f);
    lblTitle->setAnchorPoint({0.f, 0.5f});
    lblTitle->setPosition({contentLeft, 198.f});
    m_forumCreateNode->addChild(lblTitle, 1);

    m_createTitleInput = TextInput::create(panelW - 36.f, "Post title...", "chatFont.fnt");
    m_createTitleInput->setCommonFilter(CommonFilter::Any);
    m_createTitleInput->setMaxCharCount(80);
    m_createTitleInput->setPosition({cx, 180.f});
    m_createTitleInput->setScale(0.85f);
    m_forumCreateNode->addChild(m_createTitleInput, 1);

    auto lblDesc = CCLabelBMFont::create(
        tr("pai.hub.forum.desc_placeholder", "Description").c_str(), "goldFont.fnt");
    lblDesc->setScale(0.35f);
    lblDesc->setAnchorPoint({0.f, 0.5f});
    lblDesc->setPosition({contentLeft, 158.f});
    m_forumCreateNode->addChild(lblDesc, 1);

    m_createDescInput = TextInput::create(panelW - 36.f, "Description...", "chatFont.fnt");
    m_createDescInput->setCommonFilter(CommonFilter::Any);
    m_createDescInput->setMaxCharCount(500);
    m_createDescInput->setPosition({cx, 140.f});
    m_createDescInput->setScale(0.85f);
    m_forumCreateNode->addChild(m_createDescInput, 1);

    auto lblTags = CCLabelBMFont::create(
        tr("pai.hub.forum.create.tags", "Tags (tap to toggle)").c_str(), "goldFont.fnt");
    lblTags->setScale(0.32f);
    lblTags->setAnchorPoint({0.f, 0.5f});
    lblTags->setPosition({contentLeft, 118.f});
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

    m_createTagsHint = CCLabelBMFont::create(
        tr("pai.hub.forum.create.tags_hint",
           "Tap a tag above to attach it to your post").c_str(),
        "bigFont.fnt"
    );
    m_createTagsHint->setScale(0.24f);
    m_createTagsHint->setColor(kListTextSoft);
    m_createTagsHint->setOpacity(190);
    m_createTagsHint->setPosition({cx, 64.f});
    m_forumCreateNode->addChild(m_createTagsHint, 1);

    m_createTagMenu = CCMenu::create();
    m_createTagMenu->setID("inline-create-tags"_spr);
    m_createTagMenu->setVisible(false);
    m_forumCreateNode->addChild(m_createTagMenu, 1);

    auto btnMenu = makeZeroMenu();
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

}

void PaimonHubLayer::onOpenConfig(CCObject*) {
    // pushScene without TransitionManager — avoids black screen on return
    if (auto scene = PaiConfigLayer::scene()) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenProfiles(CCObject*) {
    if (auto popup = ProfilePicEditorPopup::create()) popup->show();
}

void PaimonHubLayer::onOpenBackgrounds(CCObject*) {
    if (auto popup = BackgroundConfigPopup::create()) popup->show();
}

void PaimonHubLayer::onOpenPaiDraw(CCObject*) {
    paimon::storeButtonOrigin({CCDirector::get()->getWinSize().width - 50.f, 54.f});
    if (auto scene = paidraw::PaiDrawLobbyLayer::scene()) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenExtras(CCObject*) {
    if (auto scene = PaiConfigLayer::scene()) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenSupport(CCObject*) {
    // stack so pop restores without black screen
    if (auto scene = PaimonSupportLayer::scene()) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenDiscordConfig(CCObject*) {
    if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show();
}

void PaimonHubLayer::onBack(CCObject*) {
    CCDirector::get()->replaceScene(MenuLayer::scene(false));
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
        msg->runAction(CCSequence::create(CCDelayTime::create(1.8f), CCRemoveSelf::create(), nullptr));
    };

    using S = paimon::updates::UpdateChecker::State;
    switch (checker.state()) {
        case S::Idle:
        case S::Failed:
            checker.checkAsync();
            [[fallthrough]];
        case S::Checking:
            flash(tr("pai.update.checking", "Checking for updates...").c_str(), {100, 200, 255});
            return;
        case S::UpdateAvailable:
            if (checker.downloadUrl().empty()) {
                flash(tr("pai.update.failed", "Error: no download URL").c_str(), {255, 110, 110});
                return;
            }
            if (auto popup = paimon::updates::UpdateProgressPopup::create()) popup->show();
            return;
        case S::UpToDate:
        default:
            flash(tr("pai.update.uptodate", "You're up to date!").c_str(), {120, 255, 120});
            return;
    }
}

void PaimonHubLayer::onRefreshNews(CCObject*) {
    auto& chk = paimon::updates::UpdateChecker::get();
    if (chk.state() != paimon::updates::UpdateChecker::State::Checking) chk.checkAsync();
    rebuildNewsList();
    PaimonNotify::create(tr("pai.hub.news.refreshed", "News refreshed!"), NotificationIcon::Success)->show();
}

void PaimonHubLayer::onCreatePost(CCObject*) {
    std::vector<std::string> available = m_forumTags;
    for (auto const& t : m_customTags) {
        if (!contains(available, t)) available.push_back(t);
    }

    auto popup = CreatePostPopup::create(
        std::move(available),
        [self = WeakRef<PaimonHubLayer>(this)](paimon::forum::Post const& p) {
            auto* hub = self.lock().data();
            if (!hub || !hub->getParent()) return;
            for (auto const& t : p.tags) {
                if (!contains(hub->m_forumTags, t) && !contains(hub->m_customTags, t))
                    hub->m_customTags.push_back(t);
            }
            hub->refreshTagButtons();
            hub->refreshForumPosts();
        }
    );
    if (popup) popup->show();
}

void PaimonHubLayer::onFilterByTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx >= 0 && idx < (int)m_visibleTags.size()) {
        std::string tag = m_visibleTags[idx];
        if (contains(m_selectedTags, tag)) eraseOne(m_selectedTags, tag);
        else m_selectedTags.push_back(tag);
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

    if (auto panel = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
        panel->setContentSize({260.f, 130.f});
        panel->setPosition({winSize.width / 2, winSize.height / 2});
        overlay->addChild(panel);
    } else {
        auto fallback = paimon::SpriteHelper::createDarkPanel(260, 130, 230);
        fallback->setPosition({winSize.width / 2 - 130, winSize.height / 2 - 65});
        overlay->addChild(fallback);
    }

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.create_tag", "Create Custom Tag").c_str(),
        "goldFont.fnt"
    );
    titleLbl->setScale(0.4f);
    titleLbl->setPosition({winSize.width / 2, winSize.height / 2 + 40});
    overlay->addChild(titleLbl, 1);

    m_newTagInput = TextInput::create(180, tr("pai.hub.forum.new_tag", "Tag name"));
    m_newTagInput->setPosition({winSize.width / 2 - 90, winSize.height / 2});
    m_newTagInput->setScale(0.6f);
    overlay->addChild(m_newTagInput, 51);

    auto btnMenu = makeZeroMenu("create-tag-menu"_spr);
    btnMenu->setContentSize(winSize);
    overlay->addChild(btnMenu, 52);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.55f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(PaimonHubLayer::onCloseCreateTag));
    closeBtn->setPosition({winSize.width / 2 - 130.f + 6.f, winSize.height / 2 + 65.f - 6.f});
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
    dismissOverlay(m_createTagOverlay);
    m_newTagInput = nullptr;
}

void PaimonHubLayer::onSubmitTag(CCObject*) {
    std::string tag = m_newTagInput ? m_newTagInput->getString() : "";
    if (!tag.empty() && !contains(m_forumTags, tag) && !contains(m_customTags, tag)) {
        m_customTags.push_back(tag);
        refreshTagButtons();
    }
    dismissOverlay(m_createTagOverlay);
    m_newTagInput = nullptr;
}

void PaimonHubLayer::showForumLoading() {
    if (m_forumLoadingSpinner) return;
    m_forumLoadingSpinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    m_forumLoadingSpinner->show(this, 300);
}

void PaimonHubLayer::hideForumLoading() {
    if (!m_forumLoadingSpinner) return;
    m_forumLoadingSpinner->dismiss();
    m_forumLoadingSpinner = nullptr;
}

void PaimonHubLayer::refreshForumPosts() {
    if (!m_forumPostList) return;
    m_forumPostList->removeAllChildren();
    showForumLoading();

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
            auto selfRef = self.lock();
            auto* hub = selfRef.data();
            if (!hub || !hub->getParent() || !hub->m_forumPostList) return;
            hub->hideForumLoading();
            hub->renderPosts(res.ok ? res.data : std::vector<paimon::forum::Post>{});
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

    constexpr float kPostH = 56.f;
    constexpr float kIconSize = 20.f;
    constexpr float kPad = 8.f;
    float totalH = static_cast<float>(posts.size()) * kPostH;
    if (totalH < listH) totalH = listH;

    auto scroll = ScrollLayer::create({listW, listH});
    scroll->setPosition({-listW / 2.f, -listH});
    scroll->setID("forum-scroll"_spr);
    m_forumPostList->addChild(scroll, 1);
    scroll->m_contentLayer->setContentSize({listW, totalH});

    float cardW = listW;
    float cardX = 0.f;

    float y = totalH;
    int i = 0;
    for (auto const& post : posts) {
        y -= kPostH;

        auto cardContainer = cocos2d::CCNodeRGBA::create();
        cardContainer->setContentSize({cardW, kPostH});
        cardContainer->setAnchorPoint({0.f, 0.f});
        cardContainer->setPosition({cardX, y});
        cardContainer->setCascadeOpacityEnabled(true);
        scroll->m_contentLayer->addChild(cardContainer, 1);

        auto bg = CCLayerColor::create(i % 2 == 0 ? kListRowLight : kListRowDark);
        bg->setContentSize({cardW, kPostH});
        bg->setPosition({0.f, 0.f});
        cardContainer->addChild(bg, 0);

        if (i > 0) {
            auto line = CCLayerColor::create({0, 0, 0, 60});
            line->setContentSize({cardW, 1.f});
            line->setPosition({0.f, kPostH - 1.f});
            cardContainer->addChild(line, 3);
        }

        auto cardMenu = makeZeroMenu();
        cardMenu->setContentSize({cardW, kPostH});
        cardMenu->ignoreAnchorPointForPosition(true);
        cardContainer->addChild(cardMenu, 4);

        WeakRef<PaimonHubLayer> selfRef = this;
        std::string postId = post.id;
        auto openPostDetail = [selfRef, postId]() {
            if (auto hubRef = selfRef.lock(); hubRef.data() && hubRef.data()->getParent()) {
                hubRef.data()->showForumLoading();
            }
            paimon::forum::ForumApi::get().getPost(postId,
                [selfRef](paimon::forum::Result<paimon::forum::Post> res) {
                    auto hubRef = selfRef.lock();
                    auto* hub = hubRef.data();
                    if (!hub || !hub->getParent()) return;
                    hub->hideForumLoading();
                    if (!res.ok) return;
                    auto popup = PostDetailPopup::create(res.data, [selfRef]() {
                        auto hubRef = selfRef.lock();
                        auto* hub = hubRef.data();
                        if (hub && hub->getParent()) hub->refreshForumPosts();
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

        float headerY = kPostH - kPad - kIconSize / 2.f;
        auto nameLbl = CCLabelBMFont::create(
            post.author.username.empty() ? "Anonymous" : post.author.username.c_str(),
            "goldFont.fnt"
        );
        nameLbl->setScale(0.32f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({kPad + kIconSize + 7.f, headerY});
        cardContainer->addChild(nameLbl, 1);

        auto dateLbl = CCLabelBMFont::create(
            paimon::forum::formatRelativeTime(post.createdAt).c_str(),
            "chatFont.fnt"
        );
        dateLbl->setScale(0.38f);
        dateLbl->setColor(kListTextSoft);
        dateLbl->setAnchorPoint({1.f, 0.5f});
        dateLbl->setPosition({cardW - kPad, headerY});
        cardContainer->addChild(dateLbl, 1);

        float titleY = kPostH - kPad - kIconSize - 5.f;
        auto titleLbl = CCLabelBMFont::create(
            post.title.empty() ? "(untitled)" : post.title.c_str(),
            "bigFont.fnt"
        );
        titleLbl->setScale(0.34f);
        titleLbl->setAnchorPoint({0.f, 0.5f});
        titleLbl->setPosition({kPad, titleY});
        shrinkLabelToFit(titleLbl, cardW - kPad * 2.f);
        cardContainer->addChild(titleLbl, 1);

        float footerY = 10.f;

        auto stats = CCLabelBMFont::create(
            fmt::format("{}  Likes  -  {}  Replies", post.likes, post.replyCount).c_str(),
            "bigFont.fnt"
        );
        stats->setScale(0.26f);
        stats->setColor(post.likedByMe ? ccColor3B{255, 140, 170} : ccColor3B{255, 255, 255});
        stats->setAnchorPoint({1.f, 0.5f});
        stats->setPosition({cardW - kPad, footerY});
        cardContainer->addChild(stats, 1);
        float statsLeft = cardW - kPad - stats->getScaledContentSize().width;

        float tagX = kPad;
        int tagsShown = 0;
        for (auto const& t : post.tags) {
            if (tagsShown >= 2) break;
            auto chip = ButtonSprite::create(t.c_str(), "bigFont.fnt", "GJ_button_05.png", 0.7f);
            chip->setScale(0.18f);
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
            more->setScale(0.38f);
            more->setColor(kListTextSoft);
            more->setAnchorPoint({0.f, 0.5f});
            more->setPosition({tagX, footerY});
            cardContainer->addChild(more, 2);
            tagX += more->getScaledContentSize().width + 6.f;
        } else if (tagsShown > 0) {
            tagX += 2.f;
        }

        std::string preview = post.description;
        if (preview.size() > 90) preview = preview.substr(0, 87);
        if (!preview.empty()) {
            float preMaxW = statsLeft - tagX - 10.f;
            if (preMaxW > 40.f) {
                auto pre = CCLabelBMFont::create(preview.c_str(), "chatFont.fnt");
                pre->setScale(0.36f);
                pre->setColor({255, 250, 240});
                pre->setOpacity(220);
                pre->setAnchorPoint({0.f, 0.5f});
                pre->setPosition({tagX, footerY});
                while (pre->getScaledContentSize().width > preMaxW && preview.size() > 10) {
                    preview.resize(preview.size() - 6);
                    pre->setString((preview + "...").c_str());
                }
                cardContainer->addChild(pre, 1);
            }
        }

        float delay = std::min(0.03f * i, 0.3f);
        cardContainer->setOpacity(0);
        cardContainer->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCFadeTo::create(0.18f, 255),
            nullptr
        ));

        ++i;
    }

    scroll->scrollToTop();
}

void PaimonHubLayer::refreshTagButtons() {
    if (!m_tagMenu) return;
    m_tagMenu->removeAllChildren();
    m_visibleTags = m_activePredefTags;
    m_visibleTags.insert(m_visibleTags.end(), m_customTags.begin(), m_customTags.end());
    if (m_emptyTagsHint) m_emptyTagsHint->setVisible(m_visibleTags.empty());

    for (size_t i = 0; i < m_visibleTags.size(); i++) {
        bool isSelected = contains(m_selectedTags, m_visibleTags[i]);
        std::string label = isSelected ? (m_visibleTags[i] + "  x") : m_visibleTags[i];
        auto tagSpr = ButtonSprite::create(
            label.c_str(), "bigFont.fnt",
            isSelected ? "GJ_button_01.png" : "GJ_button_04.png", .8f
        );
        tagSpr->setScale(0.32f);
        if (!isSelected) tagSpr->setColor({210, 220, 240});
        auto tagBtn = CCMenuItemSpriteExtra::create(tagSpr, this, menu_selector(PaimonHubLayer::onFilterByTag));
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

    if (auto panel = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
        panel->setAnchorPoint({0.f, 0.f});
        panel->setContentSize({panelW, panelH});
        panel->setPosition({panelX, panelY});
        m_predefPickerOverlay->addChild(panel);
    } else {
        auto fallback = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 230);
        fallback->setPosition({panelX, panelY});
        m_predefPickerOverlay->addChild(fallback);
    }

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
    hintLbl->setColor({255, 235, 190});
    hintLbl->setPosition({winSize.width / 2.f, panelY + panelH - 40.f});
    m_predefPickerOverlay->addChild(hintLbl, 1);

    auto closeMenu = makeZeroMenu("predef-picker-close"_spr);
    closeMenu->setContentSize(winSize);
    m_predefPickerOverlay->addChild(closeMenu, 52);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.55f);
    auto closeBtn = CCMenuItemSpriteExtra::create(
        closeSpr, this, menu_selector(PaimonHubLayer::onClosePredefPicker)
    );
    closeBtn->setPosition({panelX + panelW - 12.f, panelY + panelH - 12.f});
    closeMenu->addChild(closeBtn);

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
    dismissOverlay(m_predefPickerOverlay);
}

void PaimonHubLayer::onTogglePredefTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= (int)m_forumTags.size()) return;

    std::string const& tag = m_forumTags[idx];
    if (contains(m_activePredefTags, tag)) {
        eraseOne(m_activePredefTags, tag);
        eraseOne(m_selectedTags, tag);
    } else {
        m_activePredefTags.push_back(tag);
    }

    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(contains(m_activePredefTags, tag) ? "GJ_button_01.png" : "GJ_button_05.png");
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

    bool wasSelected = contains(m_createSelectedTags, tag);
    if (wasSelected) eraseOne(m_createSelectedTags, tag);
    else m_createSelectedTags.push_back(tag);

    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(wasSelected ? "GJ_button_05.png" : "GJ_button_01.png");
        }
    }

    if (!m_createTagsHint) return;
    if (m_createSelectedTags.empty()) {
        m_createTagsHint->setString(tr("pai.hub.forum.create.tags_hint",
            "Tap a tag above to attach it to your post").c_str());
        m_createTagsHint->setColor(kListTextSoft);
    } else {
        std::string joined;
        for (size_t i = 0; i < m_createSelectedTags.size(); ++i) {
            if (i) joined += ", ";
            joined += m_createSelectedTags[i];
        }
        m_createTagsHint->setString(("Selected: " + joined).c_str());
        m_createTagsHint->setColor({180, 220, 180});
    }
}

void PaimonHubLayer::onCreateSubmit(CCObject*) {
    std::string title = m_createTitleInput ? std::string(m_createTitleInput->getString()) : "";
    std::string desc  = m_createDescInput  ? std::string(m_createDescInput->getString())  : "";
    if (title.empty()) {
        PaimonNotify::create("Please enter a title", NotificationIcon::Warning)->show();
        return;
    }

    paimon::forum::CreatePostRequest req{title, desc, m_createSelectedTags};
    WeakRef<PaimonHubLayer> self = this;
    paimon::forum::ForumApi::get().createPost(req, [self](paimon::forum::Result<paimon::forum::Post> res) {
        auto* hub = self.lock().data();
        if (!hub || !hub->getParent()) return;
        if (!res.ok) {
            PaimonNotify::create(("Failed: " + res.error).c_str(), NotificationIcon::Error)->show();
            return;
        }
        PaimonNotify::create("Post published!", NotificationIcon::Success)->show();
        hub->switchForumSubTab(0);
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
