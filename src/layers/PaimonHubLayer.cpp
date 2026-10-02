#include "../features/custom-hover/CustomHover.hpp"
#include "PaimonHubLayer.hpp"
#include "../features/icon-maker/ui/IconGalleryLayer.hpp"
#include "../features/texture-studio/ui/TextureStudioLayer.hpp"
#include "../features/community/ui/CommunityHubLayer.hpp"
#include "../features/versus/ui/VersusHubLayer.hpp"
#include "../features/twitch-requests/ui/TwitchRequestsLayer.hpp"
#include "../features/guide/ui/PaimonGuideChatPopup.hpp"
#include "../ui/PaimonUI.hpp"
#include "../ui/PaiConfigKit.hpp"
#include "../utils/FluidReveal.hpp"
#include "PaimonHubData.hpp"
#include "PaiConfigLayer.hpp"
#include "PaimonSupportLayer.hpp"
#include "PaimonModulesLayer.hpp"
#include "../features/quick-hub/ui/RadialConfigPopup.hpp"
#include "../features/paidraw/PaiDrawIcon.hpp"
#include "../features/paidraw/PaiDrawUI.hpp"
#include "../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../features/profiles/ui/ProfileSettingsPopup.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../features/transitions/ui/DynamicTransitionConfigPopup.hpp"
#include "../features/cursor/ui/CursorConfigPopup.hpp"
#include "../features/pet/ui/PetConfigPopup.hpp"
#include "../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../features/progressbar/ui/ProgressBarConfigPopup.hpp"
#include "../features/custom-slider/ui/CustomSliderPopup.hpp"
#include "../features/smooth-scroll/ui/SmoothScrollConfigPopup.hpp"
#include "../features/discord-presence/ui/DiscordConfigPopup.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/beat-shaders/ui/BeatShaderConfigLayer.hpp"
#include "../features/rtx/ui/RTXConfigLayer.hpp"
#include "../features/dynamic-songs/ui/DynamicSongPopup.hpp"
#include "../features/dynamic-volume/ui/DynamicVolumePopup.hpp"
#include "../features/settings-panel/services/SettingsPanelManager.hpp"
#include "../features/settings-panel/ui/SettingsCategoryBuilder.hpp"
#include "../features/settings-panel/ui/SettingsControls.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/forum/services/ForumApi.hpp"
#include "../features/forum/ui/CreatePostPopup.hpp"
#include "../features/forum/ui/PostDetailPopup.hpp"
#include "../features/dev-tools/ui/GifToSheetPopup.hpp"
#include "../features/thumb-requests/ui/ThumbRequestsPopup.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../features/updates/ui/UpdateProgressPopup.hpp"
#include "../features/updates/ui/UpdateCenterPopup.hpp"
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
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

void animateActionCard(CCMenuItemSpriteExtra* button, float x, float y, float delay) {
    button->setPosition({x, y});
    paimon::ui::animateIn(button, delay);
}

CCSprite* makePillFace(char const* text, ccColor3B color, float scale) {
    auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
    float const width = std::clamp(label->getContentSize().width * 0.8f * scale + 16.f, 36.f, 160.f);
    return paimon::ui::makeButtonFace(text, {width, std::max(18.f, 60.f * scale)}, color, 0.8f * scale);
}

void setPillSelected(CCMenuItemSpriteExtra* button, bool selected) {
    if (!button || !button->getNormalImage()) return;
    auto* face = button->getNormalImage();
    if (auto* panel = typeinfo_cast<CCRGBAProtocol*>(face->getChildByID("paimon-button-surface"_spr))) {
        panel->setColor(selected ? ccColor3B{36, 75, 106} : paimon::ui::palette::raised);
    }
    if (auto* label = typeinfo_cast<CCRGBAProtocol*>(face->getChildByID("paimon-button-label"_spr))) {
        label->setColor(selected ? paimon::ui::palette::text : paimon::ui::palette::muted);
    }
}

CCMenuItemSpriteExtra* makeActionCardBtn(
    paimon::hubdata::HubActionMeta const& action,
    ccColor3B accent,
    float width, float height,
    WeakRef<PaimonHubLayer> self
) {
    auto* face = paimon::ui::makeButtonFace("", {width, height});
    auto* stripe = paimon::SpriteHelper::createColorPanel(3.f, height - 20.f, accent, 255, 1.f);
    stripe->setPosition({7.f, 10.f});
    face->addChild(stripe);
    auto* title = CCLabelBMFont::create(action.title.c_str(), "bigFont.fnt");
    title->setAnchorPoint({0.f, 1.f});
    title->setColor(paimon::ui::palette::text);
    title->limitLabelWidth(width - 50.f, 0.35f, 0.16f);
    title->setPosition({17.f, height - 11.f});
    face->addChild(title);
    auto* desc = CCLabelBMFont::create(action.desc.c_str(), "chatFont.fnt",
        (width - 32.f) / 0.42f, kCCTextAlignmentLeft);
    desc->setScale(0.42f);
    desc->setColor(paimon::ui::palette::muted);
    desc->setAnchorPoint({0.f, 1.f});
    desc->setPosition({17.f, height - 28.f});
    face->addChild(desc);

    auto* favoriteMenu = CCMenu::create();
    favoriteMenu->setPosition({0.f, 0.f});
    favoriteMenu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    if (star && !action.id.empty()) {
        star->setScale(0.42f);
        auto favorites = Mod::get()->getSavedValue<std::vector<std::string>>("hub-favorites", {});
        star->setColor(std::find(favorites.begin(), favorites.end(), action.id) != favorites.end()
            ? paimon::ui::palette::warning : paimon::ui::palette::muted);
        auto* pin = CCMenuItemExt::createSpriteExtra(star, [self, id = action.id](CCMenuItemSpriteExtra*) {
            if (auto hub = self.lock()) hub->toggleHubFavorite(id);
        });
        pin->setPosition({width - 17.f, height - 17.f});
        pin->setSizeMult(1.4f);
        favoriteMenu->addChild(pin);
    }
    face->addChild(favoriteMenu, 5);
    auto* button = CCMenuItemExt::createSpriteExtra(face, [self, action](CCMenuItemSpriteExtra*) {
        if (auto hub = self.lock(); hub && hub->getParent()) {
            hub->activateHubAction(action.id, action.onPress);
        }
    });
    button->m_scaleMultiplier = 1.025f;
    return button;
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

// mobile lacks the setting; hassetting is the safe check.
bool discordSupported() {
    auto* mod = geode::Mod::get();
    return mod && mod->hasSetting("discord-rpc-enabled");
}

std::vector<HubCategoryMeta> getHubCategories() {
    std::vector<HubCategoryMeta> cats = {
        {"General", "Idioma, updates y mantenimiento.", {130, 240, 170}, paimon::ui::getGeneralInfo},
        {"Miniaturas", "Layout, galeria, efectos y captura.", {120, 210, 255}, paimon::ui::getThumbnailsInfo},
        {"Nivel", "Pantalla de info, fondo y transiciones.", {170, 190, 255}, paimon::ui::getLevelInfoScreenInfo},
        {"Audio", "Profile music, menu music y capas.", {255, 165, 210}, paimon::ui::getAudioInfo},
        {"Fondos", "Fondos por capa, video y transiciones.", {140, 245, 200}, paimon::ui::getBackgroundsInfo},
        {"Extras", "Mascota, cursor, popups y rendimiento.", {255, 140, 140}, paimon::ui::getExtrasInfo},
    };
    if (discordSupported()) {
        cats.push_back({"Discord", "Rich Presence completa.", {140, 160, 255}, paimon::ui::getDiscordInfo});
    }
    cats.push_back({"Crear", "Iconos, texturas y recursos propios.", {182, 161, 255}, [] {
        return std::vector<paimon::ui::InfoSection>{
            {"Iconos", "Crea, importa y organiza tus iconos. La galeria conserva proyectos y favoritos.", {182, 161, 255}},
            {"Texturas", "Organiza paquetes y edita sus colores con una vista previa en el juego.", {116, 204, 255}},
            {"GIF a Sheet", "Convierte una animacion en una hoja de sprites para tus recursos.", {255, 208, 128}}
        };
    }});
    return cats;
}

std::vector<HubActionMeta> getHubActions(int categoryIndex) {
    // The creation category follows the optional Discord category.
    const bool discord = discordSupported();
    const int devIndex = discord ? 7 : 6;
    if (categoryIndex == devIndex) {
        return {
            {"Mis iconos", "GJ_button_05.png", [](PaimonHubLayer*) {
                paimon::icon_maker::IconGalleryLayer::open();
            }, devIndex, "Crea, importa y organiza tus iconos"},
            {"Texture Studio", "GJ_button_02.png", [](PaimonHubLayer*) {
                paimon::texture_studio::TextureStudioLayer::open();
            }, devIndex, "Edita paquetes y colores en vivo"},
            {"GIF a Sheet", "GJ_button_04.png", [](PaimonHubLayer*) {
                if (auto popup = paimon::dev::GifToSheetPopup::create()) popup->show();
            }, devIndex, "Convierte GIF en spritesheet"},
        };
    }
    switch (categoryIndex) {
        case 0:
            return {
                {"Modulos", "GJ_button_03.png", [](PaimonHubLayer*) {
                    auto scene = PaimonModulesLayer::scene();
                    if (scene) CCDirector::get()->pushScene(scene);
                }, 0, "Activa o desactiva funciones"},
                {"Configurar", "GJ_button_01.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(0); }, 0, "Idioma, updates y basicos"},
                {"Actualizaciones", "GJ_button_02.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::updates::UpdateCenterPopup::create()) popup->show();
                }, 0, "Nueva version y versiones antiguas"},
                {"Comunidad", "GJ_button_02.png", [](PaimonHubLayer*) {
                    if (auto* scene = CommunityHubLayer::scene()) CCDirector::get()->pushScene(scene);
                }, 0, "Creadores, miniaturas y mods compatibles"},
                {"Versus", "GJ_button_05.png", [](PaimonHubLayer*) {
                    if (auto* scene = paimon::versus::VersusHubLayer::scene()) CCDirector::get()->pushScene(scene);
                }, 0, "Partidas, mazos y clasificaciones"},
                {"Peticiones en directo", "GJ_button_02.png", [](PaimonHubLayer*) {
                    TwitchRequestsLayer::open();
                }, 0, "Colas y fuentes de streaming"},
                {"PaiDraw", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenPaiDraw(nullptr); }, 0, "Dibuja con la comunidad"},
                {"Soporte", "GJ_button_04.png", [](PaimonHubLayer* self) { self->onOpenSupport(nullptr); }, 0, "Ayuda y contacto"},
                {"Reiniciar ajustes", "GJ_button_03.png", [](PaimonHubLayer*) {
                    paimon::factory_reset::requestWithConfirmation();
                }, 0, "Restaura todo por defecto"},
            };
        case 1: {
            std::vector<HubActionMeta> actions = {
                {"Configurar", "GJ_button_02.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(1); }, 1, "Tamano y estilo de celdas"},
                {"Efectos", "GJ_button_03.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(2); }, 1, "Animaciones y transiciones"},
            };
            if (paimon::modules::isEnabled(paimon::thumbreq::kModuleId)) {
                actions.push_back({"Peticiones", "GJ_button_04.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::thumbreq::ThumbRequestsPopup::create()) popup->show();
                }, 1, "Lo que se pide por Discord"});
            }
            return actions;
        }
        case 2:
            return {
                {"Configurar", "GJ_button_01.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(3); }, 2, "Fondo y efectos del nivel"},
                {"Barra Progreso", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = ProgressBarConfigPopup::create()) popup->show(); }, 2, "Personaliza la barra"},
            };
        case 3:
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
                {"Volumen Dinamico", "GJ_button_05.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::dynvol::DynamicVolumePopup::create()) {
                        popup->show();
                    }
                }, 3, "Iguala el salto entre canciones"},
                {"Cancion Dinamica", "GJ_button_01.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::dynsong::DynamicSongPopup::create()) {
                        popup->show();
                    }
                }, 3, "La cancion del nivel y su buceo"},
            };
        case 4:
            return {
                {"Editor Fondos", "GJ_button_01.png", [](PaimonHubLayer* self) { self->onOpenConfig(nullptr); }, 4, "Fondo por pantalla, en vivo"},
                {"Transiciones", "GJ_button_04.png", [](PaimonHubLayer*) { if (auto popup = TransitionConfigPopup::create()) popup->show(); }, 4, "Animaciones entre escenas"},
            };
        case 5:
            return {
                {"Guia de Paimon", "GJ_button_05.png", [](PaimonHubLayer*) {
                    if (auto* popup = paimon::guide::PaimonGuideChatPopup::create()) popup->show();
                }, 5, "Ayuda para encontrar y usar las funciones"},
                {"Quick Hub", "GJ_button_02.png", [](PaimonHubLayer*) {
                    if (auto* popup = paimon::quickhub::RadialConfigPopup::create()) popup->show();
                }, 5, "Tus accesos rapidos en un menu radial"},
                {"Smooth UI", "GJ_button_05.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::ui::SmoothUIConfigPopup::create()) popup->show();
                }, 5, "Animaciones suaves"},
                {"Dynamic Transition", "GJ_button_04.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::transitions::dynamic::DynamicTransitionConfigPopup::create()) popup->show();
                }, 5, "Los layers se abren desde su boton"},
                {"Mascota", "GJ_button_03.png", [](PaimonHubLayer*) { if (auto popup = PetConfigPopup::create()) popup->show(); }, 5, "Companero en pantalla"},
                {"Cursor", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = CursorConfigPopup::create()) popup->show(); }, 5, "Cursor personalizado"},
                {"Hover", "GJ_button_01.png", [](PaimonHubLayer*) { paimon::hover::open(); }, 5, "Animaciones al pasar cursor o touch"},
                {"Slider", "GJ_button_01.png", [](PaimonHubLayer*) { if (auto popup = paimon::slider::CustomSliderPopup::create()) popup->show(); }, 5, "Barra de scroll custom"},
                {"Scroll", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = paimon::smoothscroll::SmoothScrollConfigPopup::create()) popup->show(); }, 5, "Desplazamiento suave"},
                {"Beat Shaders", "GJ_button_04.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::beat_shaders::BeatShaderConfigLayer::create()) {
                        popup->show();
                    }
                }, 5, "Fondos al ritmo"},
                {"Paimon RTX", "GJ_button_01.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::rtx::RTXConfigLayer::create()) {
                        popup->show();
                    }
                }, 5, "Luz trazada en todo el juego"},
                {"Perfil", "GJ_button_05.png", [](PaimonHubLayer* self) { self->onOpenProfiles(nullptr); }, 5, "Editor de foto de perfil"},
            };
        case 6: // unreachable without discord.
            if (!discord) return {};
            return {
                {"Configurar", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show(); }, 6, "Rich Presence a tu gusto"},
                {"Refrescar", "GJ_button_05.png", [](PaimonHubLayer*) { paimon::discord::DiscordPresenceManager::get().refreshSoon(); PaimonNotify::create("Rich Presence actualizada.", NotificationIcon::Success)->show(); }, 6, "Fuerza la actualizacion"},
            };
        default:
            return {};
    }
}

std::vector<GranularSettingMeta> getGranularSettings() {
    std::vector<GranularSettingMeta> settings = {
        {"Language / Idioma", "Idioma / Language", 0},
        {"Auto Update", "Auto Actualizar", 0},
        {"Quick Search Key", "Tecla de Busqueda Rapida", 0},
        {"Realtime Search Preview", "Vista Previa en Tiempo Real", 0},
        {"Settings Panel Keybind", "Tecla de Panel de Ajustes", 0},
        {"Layout Editor Keybind", "Tecla de Editor de Layout", 0},
        {"Debug Logs", "Registros de Depuracion", 0},

        {"Thumbnail Size", "Tamano de Miniatura", 1},
        {"Background Style (Cell)", "Estilo de Fondo de Celda", 1},
        {"Background Blur (Cell)", "Desenfoque de Fondo de Celda", 1},
        {"Darkness (Cell)", "Oscuridad de Celda", 1},
        {"Show Separator", "Mostrar Separador", 1},
        {"Show View Button", "Mostrar Boton de Ver", 1},
        {"Compact Mode", "Modo Compacto", 1},
        {"Show Compact Toggle", "Mostrar Boton de Modo Compacto", 1},
        {"Auto-Cycle Gallery", "Auto-Ciclo de Galeria", 1},
        {"Transition Type", "Tipo de Transicion", 1},
        {"Transition Duration", "Duracion de Transicion", 1},
        {"Hover Effects", "Efectos al pasar el mouse", 1},
        {"Animation Type", "Tipo de Animacion", 1},
        {"Animation Speed", "Velocidad de Animacion", 1},
        {"Color Effect", "Efecto de Color", 1},
        {"Effect on Background", "Efecto en Fondo", 1},
        {"Enable Capture Button", "Activar Boton de Captura", 1},
        {"Capture Thumbnail Key", "Tecla de Capturar Miniatura", 1},

        {"Background Style (Level)", "Estilo de Fondo de Nivel", 2},
        {"Dynamic Song", "Cancion Dinamica", 2},
        {"Progress Bar", "Barra de Progreso", 2},

        {"Enable Profile Music", "Activar Musica de Perfil", 3},
        {"Enable Menu Music Player", "Activar Reproductor de Musica de Menu", 3},
        {"Menu Loop Shuffle", "Mezclar Bucles de Menu", 3},
        {"Now Playing Notifications", "Notificaciones de Reproduccion", 3},
        {"Notification Duration", "Duracion de la Notificacion", 3},
        {"Notification Prefix", "Prefijo de Notificacion", 3},
        {"Seek Step (ms)", "Paso de Busqueda (ms)", 3},
        {"Show Playback Progress", "Mostrar Progreso de Reproduccion", 3},
        {"Menu Music Hotkeys", "Teclas Rapidas de Musica", 3},
        {"Remember Last Menu Loop", "Recordar Ultimo Bucle de Menu", 3},
        {"Randomize on Level Exit", "Aleatorio al Salir del Nivel", 3},
        {"Restore Position on Level Exit", "Restaurar Posicion al Salir de Nivel", 3},
        {"Randomize on Editor Exit", "Aleatorio al Salir del Editor", 3},
        {"Restore Position on Editor Exit", "Restaurar Posicion al Salir del Editor", 3},

        {"Editor Fondos", "Editor de Fondos", 4},
        {"Transiciones de Fondos", "Transiciones de Fondos", 4},
        {"Configuracion Completa", "Configuracion Completa", 4},

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
        {"Score Cell Style", "Estilo de Celda de Puntuacion", 5},
        {"Custom Hover", "Animaciones Hover", 5},
        {"Custom Slider Thumb", "Barra de Desplazamiento Personalizada", 5},
        {"Dynamic Popups", "Popups Dinamicos", 5},
        {"Dynamic Transition", "Transicion Dinamica", 5},
        {"Dynamic Popup Exit", "Salida de Popup Dinamica", 5},
        {"Popup Blur", "Desenfoque de Popup", 5},
        {"Download Threads", "Hilos de Descarga / Hilos de Red", 5},
        {"Disk Cache", "Cache de Disco", 5},
        {"Clear Cache on Exit", "Limpiar Cache al Salir", 5},
        {"Open Thumbnails Folder", "Abrir Carpeta de Miniaturas", 5},

    };
    // hide discord rows on mobile to keep indices stable.
    if (discordSupported()) {
        settings.push_back({"Enable Discord Rich Presence", "Activar Discord Rich Presence", 6});
        settings.push_back({"Configure Discord RPC", "Configurar Discord RPC", 6});
        settings.push_back({"Refresh Discord Status", "Refrescar Estado de Discord", 6});
    }
    return settings;
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
    if (auto* layer = PaimonHubLayer::create()) scene->addChild(layer);
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
    this->setMouseEnabled(true);
    this->scheduleUpdate();

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

    paimon::ui::decorateScene(this);
    m_bgColorHome = paimon::ui::palette::background;
    m_bgColorSub = paimon::ui::palette::background;

    m_mainMenu = makeZeroMenu("paimon-hub-main-menu"_spr);
    this->addChild(m_mainMenu, 10);

    auto title = CCLabelBMFont::create(tr("pai.hub.title", "Paimbnails").c_str(), "bigFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({46.f, top - 20.f});
    title->limitLabelWidth(winSize.width < 500.f ? 76.f : 105.f, 0.48f, 0.22f);
    this->addChild(title);

    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaimonHubLayer::onBack));
    backBtn->setPosition({20.f, top - 20.f});
    backBtn->setScale(0.75f);
    m_mainMenu->addChild(backBtn);

    auto helpSpr = paimon::ui::makeButtonFace("?", {24.f, 24.f});
    auto helpBtn = CCMenuItemSpriteExtra::create(helpSpr, this, menu_selector(PaimonHubLayer::onOpenHelp));
    helpBtn->setPosition({winSize.width - 20.f, top - 20.f});
    m_mainMenu->addChild(helpBtn);

    auto uiSpr = paimon::ui::makeButtonFace("GD", {30.f, 24.f});
    auto uiBtn = CCMenuItemSpriteExtra::create(uiSpr, this, menu_selector(PaimonHubLayer::onToggleUIStyle));
    uiBtn->setID("ui-style-btn"_spr);
    uiBtn->setPosition({winSize.width - 56.f, top - 20.f});
    m_mainMenu->addChild(uiBtn);

    auto updSpr = paimon::ui::makeButtonFace(tr("pai.hub.btn.updates", "Updates").c_str(),
        {winSize.width < 500.f ? 48.f : 70.f, 24.f});
    auto updBtn = CCMenuItemSpriteExtra::create(updSpr, this, menu_selector(PaimonHubLayer::onCheckUpdate));
    updBtn->setID("updates-btn"_spr);
    updBtn->setPosition({winSize.width - (winSize.width < 500.f ? 98.f : 114.f), top - 20.f});
    m_mainMenu->addChild(updBtn);

    float tabY = top - 20.f;
    std::vector<std::string> tabNames = {
        tr("pai.hub.tab.home", "Home"),
        tr("pai.hub.tab.news", "News"),
        tr("pai.hub.tab.forum", "Forum")
    };

    auto tabBar = CCMenu::create();
    tabBar->setID("paimon-hub-tab-bar"_spr);
    tabBar->setPosition({cx + (winSize.width < 500.f ? 2.f : 4.f), tabY});
    tabBar->setContentSize({174.f, 24.f});
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
        auto spr = paimon::ui::makeButtonFace(tabNames[i].c_str(), {54.f, 24.f}, paimon::ui::palette::raised, 0.30f);
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
        tab->setCascadeOpacityEnabled(false);
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
    this->schedule(schedule_selector(PaimonHubLayer::refreshUpdateBadge), 1.f);
    refreshUpdateBadge(0.f);
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
    // scene(false) avoids black screen on escape.
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
    // sidebar buttons use tag 100+i.
    if (idx >= 100) {
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

    auto* activeTab = idx == 0 ? m_homeTab : idx == 1 ? m_newsTab : m_forumTab;
    auto* activeMenu = idx == 0 ? m_homeMenu : idx == 1 ? m_newsMenu : m_forumMenu;
    if (activeTab) {
        activeTab->stopAllActions();
        activeTab->setPosition({0.f, 0.f});
        activeTab->setOpacity(255);
        if (idx != prev) paimon::fluid::revealNode(activeTab);
    }
    if (activeMenu) {
        activeMenu->stopAllActions();
        activeMenu->setPosition({0.f, 0.f});
    }
    std::vector<std::string> const names = {
        tr("pai.hub.tab.home", "Home"), tr("pai.hub.tab.news", "News"), tr("pai.hub.tab.forum", "Forum")};
    for (size_t i = 0; i < m_tabBtns.size(); ++i) {
        if (m_gdMode) {
            if (auto* sprite = typeinfo_cast<ButtonSprite*>(m_tabBtns[i]->getNormalImage())) {
                sprite->setColor(static_cast<int>(i) == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
            }
        } else {
            m_tabBtns[i]->setSprite(paimon::ui::makeButtonFace(names[i].c_str(), {54.f, 24.f},
                static_cast<int>(i) == idx ? ccColor3B{36, 75, 106} : paimon::ui::palette::raised, 0.30f));
        }
    }
    if (idx == 2) refreshForumPosts();
}

void PaimonHubLayer::buildHomeTab() {
    auto const win = CCDirector::get()->getWinSize();
    float const panelH = win.height - 58.f;
    float const top = win.height - 70.f;
    auto categories = getHubCategories();
    m_sidebarBg = paimon::ui::makeSurface({135.f, panelH});
    m_sidebarBg->setPosition({15.f, 15.f});
    m_homeTab->addChild(m_sidebarBg);
    m_sidebarHighlight = paimon::ui::makeSurface({119.f, 27.f}, {36, 75, 106});
    m_sidebarHighlight->setPosition({23.f, top - 13.5f});
    m_homeTab->addChild(m_sidebarHighlight, 1);
    m_sidebarMenu = makeZeroMenu("paimon-sidebar-menu"_spr);
    m_homeTab->addChild(m_sidebarMenu, 3);
    float const spacing = std::min(28.f, (panelH - 84.f) / std::max(1.f, static_cast<float>(categories.size() - 1)));
    for (size_t i = 0; i < categories.size(); ++i) {
        float const y = top - static_cast<float>(i) * spacing;
        auto* face = CCSprite::create();
        face->setContentSize({119.f, 27.f});
        auto* button = CCMenuItemSpriteExtra::create(face, this, menu_selector(PaimonHubLayer::onTabSwitch));
        button->setPosition({82.5f, y});
        button->setTag(100 + static_cast<int>(i));
        button->m_scaleMultiplier = 1.f;
        m_sidebarMenu->addChild(button);
        auto* label = CCLabelBMFont::create(categories[i].title.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({35.f, y});
        label->limitLabelWidth(97.f, 0.32f, 0.16f);
        m_homeTab->addChild(label, 2);
        m_sidebarLabels.push_back(label);
    }
    auto* quick = paimon::ui::makeButton("Quick Hub", {112.f, 26.f}, [] {
        if (auto* popup = paimon::quickhub::RadialConfigPopup::create()) popup->show();
    });
    quick->setPosition({82.5f, 40.f});
    m_sidebarMenu->addChild(quick);
    auto* version = CCLabelBMFont::create(paimon::updates::UpdateChecker::get().localVersion().c_str(), "chatFont.fnt");
    version->setScale(0.42f);
    version->setColor(paimon::ui::palette::muted);
    version->setPosition({82.5f, 21.f});
    m_homeTab->addChild(version, 2);

    float const detailsW = win.width - 175.f;
    m_detailsBg = paimon::ui::makeSurface({detailsW, panelH});
    m_detailsBg->setPosition({160.f, 15.f});
    m_homeTab->addChild(m_detailsBg);
    m_homeCategoryTitle = CCLabelBMFont::create("", "bigFont.fnt");
    m_homeCategoryTitle->setAnchorPoint({0.f, 0.5f});
    m_homeCategoryTitle->setPosition({174.f, top});
    m_homeTab->addChild(m_homeCategoryTitle, 2);
    m_homeCategoryDesc = CCLabelBMFont::create("", "chatFont.fnt");
    m_homeCategoryDesc->setAnchorPoint({0.f, 0.5f});
    m_homeCategoryDesc->setColor(paimon::ui::palette::muted);
    m_homeCategoryDesc->setPosition({174.f, top - 19.f});
    m_homeTab->addChild(m_homeCategoryDesc, 2);

    auto* infoSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
    if (infoSprite) {
        infoSprite->setScale(0.45f);
        m_homeCategoryInfoBtn = CCMenuItemExt::createSpriteExtra(infoSprite, [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
            if (auto hub = self.lock()) {
                auto cats = getHubCategories();
                if (auto* popup = paimon::ui::FeatureInfoPopup::create(cats[hub->m_homeSelectedCategory].title,
                    cats[hub->m_homeSelectedCategory].getInfo())) popup->show();
            }
        });
        m_homeCategoryInfoBtn->setPosition({win.width - 32.f, top});
        m_sidebarMenu->addChild(m_homeCategoryInfoBtn);
    }
    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    float const searchW = detailsW - 54.f;
    m_searchInput = TextInput::create(searchW, es ? "Buscar funciones y ajustes..." : "Find features and settings...", "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setMaxCharCount(64);
    m_searchInput->setPosition({174.f + searchW / 2.f, top - 46.f});
    m_searchInput->setCallback([self = WeakRef<PaimonHubLayer>(this)](std::string const&) {
        if (auto hub = self.lock(); hub && hub->getParent()) hub->rebuildHomeCategoryCards();
    });
    m_homeTab->addChild(m_searchInput, 10);
    auto* clear = paimon::ui::makeButton("x", {24.f, 26.f}, [this] {
        m_searchInput->setString("");
        rebuildHomeCategoryCards();
    });
    clear->setPosition({win.width - 32.f, top - 46.f});
    m_sidebarMenu->addChild(clear);
    buildHomeFilterBar();
    m_homeActionsMenu = makeZeroMenu();
    m_homeMenu->addChild(m_homeActionsMenu);
    switchHomeCategory(std::clamp(Mod::get()->getSavedValue<int>("hub-last-category", 0),
        0, static_cast<int>(categories.size()) - 1));
}

void PaimonHubLayer::buildHomeFilterBar() {
    if (m_homeFilterBar) m_homeFilterBar->removeFromParent();
    auto const win = CCDirector::get()->getWinSize();
    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    m_homeFilterBar = paimon::configkit::makeTabBar(win.width - 203.f,
        {es ? "Categoria" : "Category", es ? "Favoritos" : "Favorites", es ? "Recientes" : "Recent"},
        m_homeFilter, [this](int filter) {
            m_homeFilter = filter;
            rebuildHomeCategoryCards();
        });
    m_homeFilterBar->setPosition({174.f, win.height - 152.f});
    m_homeTab->addChild(m_homeFilterBar, 3);
}

void PaimonHubLayer::switchHomeCategory(int index) {
    auto categories = getHubCategories();
    if (index < 0 || index >= static_cast<int>(categories.size())) return;
    m_homeSelectedCategory = index;
    if (m_homeFilter != 0) {
        m_homeFilter = 0;
        buildHomeFilterBar();
    }
    Mod::get()->setSavedValue("hub-last-category", index);
    if (m_searchInput) m_searchInput->setString("");
    refreshHomeCategorySelector();
    rebuildHomeCategoryCards();
}

void PaimonHubLayer::refreshHomeCategorySelector() {
    auto const win = CCDirector::get()->getWinSize();
    auto categories = getHubCategories();
    float const spacing = std::min(28.f, (win.height - 142.f) / std::max(1.f, static_cast<float>(categories.size() - 1)));
    if (m_sidebarHighlight) {
        m_sidebarHighlight->stopAllActions();
        CCPoint const destination{23.f, win.height - 83.5f - m_homeSelectedCategory * spacing};
        if (paimon::ui::motionEnabled()) {
            m_sidebarHighlight->runAction(CCEaseSineOut::create(CCMoveTo::create(paimon::ui::motionDuration(0.18f), destination)));
        } else m_sidebarHighlight->setPosition(destination);
    }
    for (size_t i = 0; i < m_sidebarLabels.size(); ++i) {
        m_sidebarLabels[i]->setColor(static_cast<int>(i) == m_homeSelectedCategory
            ? paimon::ui::palette::text : paimon::ui::palette::muted);
    }
}

void PaimonHubLayer::activateHubAction(std::string const& id, std::function<void(PaimonHubLayer*)> const& callback) {
    if (!id.empty()) {
        auto recent = Mod::get()->getSavedValue<std::vector<std::string>>("hub-recent", {});
        std::erase(recent, id);
        recent.insert(recent.begin(), id);
        if (recent.size() > 12) recent.resize(12);
        Mod::get()->setSavedValue("hub-recent", recent);
        if (m_homeFilter == 2) {
            Loader::get()->queueInMainThread([self = WeakRef<PaimonHubLayer>(this)] {
                if (auto hub = self.lock(); hub && hub->getParent()) hub->rebuildHomeCategoryCards(false);
            });
        }
    }
    if (callback) callback(this);
}

void PaimonHubLayer::toggleHubFavorite(std::string const& id) {
    auto favorites = Mod::get()->getSavedValue<std::vector<std::string>>("hub-favorites", {});
    if (contains(favorites, id)) std::erase(favorites, id);
    else favorites.push_back(id);
    Mod::get()->setSavedValue("hub-favorites", favorites);
    Loader::get()->queueInMainThread([self = WeakRef<PaimonHubLayer>(this)] {
        if (auto hub = self.lock(); hub && hub->getParent()) hub->rebuildHomeCategoryCards(false);
    });
}

void PaimonHubLayer::scrollWheel(float x, float y) {
    if (m_currentTab == 0 && paimon::configkit::queueWheelScroll(m_homeActionsScroll,
        x, y, m_homeScrollTarget, m_homeScrollTargetSet)) return;
    CCLayer::scrollWheel(x, y);
}

void PaimonHubLayer::update(float dt) {
    paimon::configkit::stepWheelScroll(m_homeActionsScroll, m_homeScrollTarget, m_homeScrollTargetSet, dt);
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

    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    auto body = fmt::format(fmt::runtime(es
        ? "Busca funciones por nombre, descripcion o ajuste.\n\n"
          "Toca la estrella para guardar un favorito. Recientes muestra tus ultimos doce accesos.\n\n"
          "Pulsa el valor de un selector para buscar entre sus opciones.\n\n"
          "<cy>Esc</c>: cerrar. <cy>{}</c>: layout de menu/pausa, configurable en Geode."
        : "Find features by name, description or setting.\n\n"
          "Tap a star to save a favorite. Recent shows your last twelve visits.\n\n"
          "Tap a selector value to search its options.\n\n"
          "<cy>Esc</c>: close. <cy>{}</c>: menu/pause layout, configurable in Geode."),
        layoutKeybind);

    PopupManager::get().alert(es ? "Ayuda del hub" : "Hub help", body).showInstant();
}

void PaimonHubLayer::rebuildHomeCategoryCards(bool resetScroll) {
    if (!m_homeActionsMenu) return;
    float const previousOffset = m_homeActionsScroll && m_homeActionsScroll->m_contentLayer
        ? m_homeActionsScroll->m_contentLayer->getPositionY()
            + m_homeActionsScroll->m_contentLayer->getContentSize().height
            - m_homeActionsScroll->getContentSize().height : 0.f;
    m_homeActionsMenu->removeAllChildren();
    if (m_homeActionsScroll) {
        m_homeActionsScroll->removeFromParent();
        m_homeActionsScroll = nullptr;
    }
    m_homeScrollTargetSet = false;
    auto categories = getHubCategories();
    auto const& category = categories[m_homeSelectedCategory];
    std::string const query = toLower(m_searchInput ? m_searchInput->getString() : "");
    auto favorites = Mod::get()->getSavedValue<std::vector<std::string>>("hub-favorites", {});
    auto recent = Mod::get()->getSavedValue<std::vector<std::string>>("hub-recent", {});
    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    std::vector<HubActionMeta> actions;
    auto include = [&](HubActionMeta action, int categoryIndex) {
        action.categoryIndex = categoryIndex;
        if (m_homeFilter == 1 && !contains(favorites, action.id)) return;
        if (m_homeFilter == 2 && !contains(recent, action.id)) return;
        if (!query.empty() && toLower(action.title + " " + action.desc + " " + categories[categoryIndex].title).find(query) == std::string::npos) return;
        if (m_homeFilter == 0 && query.empty() && categoryIndex != m_homeSelectedCategory) return;
        if (m_homeFilter != 0 || !query.empty()) action.desc = categories[categoryIndex].title + " / " + action.desc;
        actions.push_back(std::move(action));
    };
    for (size_t cat = 0; cat < categories.size(); ++cat) {
        for (auto action : getHubActions(static_cast<int>(cat))) {
            action.id = categories[cat].title + "/" + action.title;
            include(std::move(action), static_cast<int>(cat));
        }
    }
    if (!query.empty() || m_homeFilter != 0) {
        for (auto const& setting : getGranularSettings()) {
            if (setting.categoryIndex >= static_cast<int>(categories.size())) continue;
            HubActionMeta action;
            action.id = "setting/" + setting.englishName;
            action.title = es ? setting.spanishName : setting.englishName;
            action.desc = es ? setting.englishName : setting.spanishName;
            action.onPress = [setting](PaimonHubLayer*) {
                paimon::ui::openFeatureConfigFor(setting.englishName, setting.categoryIndex);
            };
            include(std::move(action), setting.categoryIndex);
        }
    }
    if (m_homeFilter == 2) {
        std::stable_sort(actions.begin(), actions.end(), [&recent](auto const& left, auto const& right) {
            return std::find(recent.begin(), recent.end(), left.id) < std::find(recent.begin(), recent.end(), right.id);
        });
    }
    std::string title = !query.empty() ? (es ? "Resultados" : "Search results")
        : m_homeFilter == 1 ? (es ? "Favoritos" : "Favorites")
        : m_homeFilter == 2 ? (es ? "Recientes" : "Recent") : category.title;
    auto const win = CCDirector::get()->getWinSize();
    float const detailsW = win.width - 175.f;
    m_homeCategoryTitle->setString(title.c_str());
    m_homeCategoryTitle->limitLabelWidth(detailsW - 58.f, 0.48f, 0.20f);
    m_homeCategoryTitle->setColor(m_homeFilter == 0 && query.empty() ? category.color : paimon::ui::palette::text);
    m_homeCategoryDesc->setString((query.empty() && m_homeFilter == 0 ? category.shortDesc
        : fmt::format(fmt::runtime(es ? "{} funciones disponibles" : "{} features available"), actions.size())).c_str());
    m_homeCategoryDesc->limitLabelWidth(detailsW - 28.f, 0.44f, 0.20f);
    if (m_homeCategoryInfoBtn) m_homeCategoryInfoBtn->setVisible(query.empty() && m_homeFilter == 0);

    float const scrollW = detailsW - 28.f;
    float const scrollH = std::max(40.f, win.height - 183.f);
    m_homeActionsScroll = ScrollLayer::create({scrollW, scrollH});
    m_homeActionsScroll->setPosition({174.f, 25.f});
    m_homeActionsScroll->setID("search-actions-scroll"_spr);
    m_homeTab->addChild(m_homeActionsScroll, 3);
    auto* content = m_homeActionsScroll->m_contentLayer;
    if (actions.empty()) {
        content->setContentSize({scrollW, scrollH});
        char const* message = m_homeFilter == 1 && query.empty()
            ? (es ? "Toca una estrella para guardar una funcion." : "Tap a star to save a feature.")
            : m_homeFilter == 2 && query.empty()
            ? (es ? "Tus ultimas funciones apareceran aqui." : "Your recent features will appear here.")
            : (es ? "Sin resultados. Prueba otro nombre o categoria." : "No results. Try another name or category.");
        auto* hint = paimon::configkit::makeHint(scrollW, message);
        hint->setPosition({0.f, (scrollH - hint->getContentSize().height) / 2.f});
        content->addChild(hint);
        return;
    }
    int const columns = std::max(1, static_cast<int>((scrollW + 8.f) / 166.f));
    float const cardW = (scrollW - 8.f * (columns - 1)) / columns;
    float cardH = 64.f;
    for (auto const& action : actions) {
        auto* label = CCLabelBMFont::create(action.desc.c_str(), "chatFont.fnt",
            (cardW - 32.f) / 0.42f, kCCTextAlignmentLeft);
        cardH = std::max(cardH, 38.f + label->getContentSize().height * 0.42f);
    }
    int const rows = (static_cast<int>(actions.size()) + columns - 1) / columns;
    float const contentH = std::max(scrollH, rows * (cardH + 8.f));
    content->setContentSize({scrollW, contentH});
    auto* menu = makeZeroMenu();
    content->addChild(menu);
    for (size_t i = 0; i < actions.size(); ++i) {
        int const column = static_cast<int>(i) % columns;
        int const row = static_cast<int>(i) / columns;
        auto* button = makeActionCardBtn(actions[i], categories[actions[i].categoryIndex].color,
            cardW, cardH, WeakRef<PaimonHubLayer>(this));
        menu->addChild(button);
        CCPoint const position{cardW / 2.f + column * (cardW + 8.f),
            contentH - cardH / 2.f - row * (cardH + 8.f)};
        if (resetScroll) animateActionCard(button, position.x, position.y, 0.02f * static_cast<float>(i));
        else button->setPosition(position);
    }
    if (resetScroll) m_homeActionsScroll->moveToTop();
    else content->setPositionY(std::clamp(scrollH - contentH + previousOffset, scrollH - contentH, 0.f));
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

    constexpr cocos2d::ccColor4B kListRowDark  = {20, 28, 45, 255};
    constexpr cocos2d::ccColor4B kListRowLight = {29, 40, 61, 255};
    constexpr auto kListTextSoft = paimon::ui::palette::muted;

    cocos2d::CCNode* makeGDPanel(cocos2d::CCNode* parent, float panelW, float panelH) {
        auto* panel = paimon::ui::makeSurface({panelW, panelH});
        panel->setPosition({15.f, 15.f});
        parent->addChild(panel, 0);
        return panel;
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

    }

    cocos2d::CCNode* makeGDRefreshSprite() {
        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png")) {
            spr->setScale(0.72f);
            return spr;
        }
        return paimon::ui::makeButtonFace("R", {28.f, 28.f});
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
        tr("pai.hub.news.title", "Latest News").c_str(), "bigFont.fnt"
    );
    titleLbl->setScale(0.48f);
    titleLbl->setColor(paimon::ui::palette::text);
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
    auto* content = m_newsScroll->m_contentLayer;
    content->removeAllChildren();
    auto const items = buildNewsItems();
    float const listW = m_newsScroll->getContentSize().width;
    float const listH = m_newsScroll->getContentSize().height;
    float totalH = 0.f;
    std::vector<CCNodeRGBA*> rows;
    for (size_t i = 0; i < items.size(); ++i) {
        auto* desc = CCLabelBMFont::create(items[i].desc.c_str(), "chatFont.fnt",
            (listW - 28.f) / 0.46f, kCCTextAlignmentLeft);
        desc->setScale(0.46f);
        desc->setColor(paimon::ui::palette::muted);
        desc->setAnchorPoint({0.f, 1.f});
        float const rowH = std::max(52.f, 38.f + desc->getScaledContentSize().height);
        auto* row = CCNodeRGBA::create();
        row->setContentSize({listW, rowH});
        row->setAnchorPoint({0.f, 0.f});
        row->setCascadeOpacityEnabled(true);
        row->addChild(paimon::ui::makeSurface({listW, rowH}, paimon::ui::palette::raised), -1);
        desc->setPosition({14.f, rowH - 28.f});
        row->addChild(desc);
        auto* title = CCLabelBMFont::create(items[i].title.c_str(), "bigFont.fnt");
        title->setColor(paimon::ui::palette::text);
        title->setAnchorPoint({0.f, 1.f});
        title->limitLabelWidth(listW - (items[i].highlight ? 75.f : 28.f), 0.36f, 0.16f);
        title->setPosition({14.f, rowH - 9.f});
        row->addChild(title);
        if (items[i].highlight) {
            auto* badge = CCLabelBMFont::create("NEW", "bigFont.fnt");
            badge->setScale(0.25f);
            badge->setColor(paimon::ui::palette::success);
            badge->setAnchorPoint({1.f, 0.5f});
            badge->setPosition({listW - 12.f, rowH - 16.f});
            row->addChild(badge);
        }
        totalH += rowH + 6.f;
        rows.push_back(row);
        content->addChild(row);
    }
    float const contentH = std::max(listH, totalH);
    content->setContentSize({listW, contentH});
    float y = contentH;
    for (size_t i = 0; i < rows.size(); ++i) {
        y -= rows[i]->getContentSize().height;
        rows[i]->setPosition({0.f, y});
        paimon::fluid::RevealOpts reveal;
        reveal.startDelay = 0.02f * static_cast<float>(i);
        paimon::fluid::revealNode(rows[i], reveal);
        y -= 6.f;
    }
    m_newsScroll->moveToTop();
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
        auto spr = makePillFace(text, paimon::ui::actionColor(bg), scale);
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

    m_forumHeaderTitle = CCLabelBMFont::create(
        tr("pai.hub.forum.title", "Community Forum").c_str(),
        "bigFont.fnt"
    );
    m_forumHeaderTitle->setScale(0.46f);
    m_forumHeaderTitle->setColor(paimon::ui::palette::text);
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
            setPillSelected(m_sortBtns[i], static_cast<int>(m_sortMode) == static_cast<int>(i));
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

        auto newPostSpr = makePillFace(tr("pai.hub.forum.create.cta", "+ New Post").c_str(),
            paimon::ui::actionColor("GJ_button_01.png"), 0.42f);
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
           "No active filter - tap [Predef] or [+] to add tags").c_str(),
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
           "No posts here yet - tap +New Post to start the conversation.").c_str(),
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
        tr("pai.hub.forum.title_placeholder", "Title").c_str(), "bigFont.fnt");
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
        tr("pai.hub.forum.desc_placeholder", "Description").c_str(), "bigFont.fnt");
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
        tr("pai.hub.forum.create.tags", "Tags (tap to toggle)").c_str(), "bigFont.fnt");
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
        auto chipSpr = makePillFace(m_forumTags[i].c_str(), paimon::ui::palette::raised, 0.26f);
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

    auto cancelSpr = paimon::ui::makeButtonFace(tr("pai.hub.forum.cancel", "Cancel").c_str(),
        {100.f, 28.f}, paimon::ui::palette::raised);
    auto cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(PaimonHubLayer::onForumSubTabSwitch)
    );
    cancelBtn->setTag(0);
    cancelBtn->setPosition({cx - 70.f, 32.f});
    btnMenu->addChild(cancelBtn);

    auto submitSpr = paimon::ui::makeButtonFace(tr("pai.hub.forum.publish", "Publish").c_str(),
        {100.f, 28.f}, paimon::ui::actionColor("GJ_button_01.png"));
    auto submitBtn = CCMenuItemSpriteExtra::create(
        submitSpr, this, menu_selector(PaimonHubLayer::onCreateSubmit)
    );
    submitBtn->setPosition({cx + 70.f, 32.f});
    btnMenu->addChild(submitBtn);

}

void PaimonHubLayer::onOpenConfig(CCObject*) {
    // no transition avoids black screen on return.
    if (auto scene = PaiConfigLayer::scene()) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenProfiles(CCObject*) {
    if (auto popup = ProfilePicEditorPopup::create()) popup->show();
}

void PaimonHubLayer::onOpenBackgrounds(CCObject*) {
    // kept for compatibility; backgrounds use paiconfiglayer.
    onOpenConfig(nullptr);
}

void PaimonHubLayer::onOpenPaiDraw(CCObject*) {
    paimon::storeButtonOrigin({CCDirector::get()->getWinSize().width - 50.f, 54.f});
    if (auto scene = paidraw::PaiDrawLobbyLayer::scene()) CCDirector::get()->pushScene(scene);
}

void PaimonHubLayer::onOpenSupport(CCObject*) {
    // pushscene keeps back-stack to avoid black screen.
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
    if (checker.state() == paimon::updates::UpdateChecker::State::Idle) {
        checker.checkAsync();
    }
    if (auto popup = paimon::updates::UpdateCenterPopup::create()) popup->show();
}

void PaimonHubLayer::refreshUpdateBadge(float) {
    if (!m_mainMenu) return;
    auto* btn = m_mainMenu->getChildByID("updates-btn"_spr);
    if (!btn) return;

    auto& chk = paimon::updates::UpdateChecker::get();
    bool wanted = chk.hasUpdate() || chk.hasPendingInstall();
    auto* dot = btn->getChildByID("updates-btn-dot"_spr);

    if (wanted && !dot) {
        auto* mark = paimon::SpriteHelper::createColorPanel(9.f, 9.f, {255, 70, 70}, 255, 4.5f);
        mark->setID("updates-btn-dot"_spr);
        mark->setAnchorPoint({0.5f, 0.5f});
        auto size = btn->getContentSize();
        mark->setPosition({size.width - 3.f, size.height - 3.f});
        btn->addChild(mark, 100);
        if (paimon::ui::motionEnabled()) mark->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(0.45f, 1.1f),
            CCScaleTo::create(0.45f, 1.f),
            nullptr
        )));
    } else if (!wanted && dot) {
        dot->removeFromParent();
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

    auto* panel = paimon::ui::makeSurface({260.f, 130.f});
    panel->setPosition(winSize / 2.f - CCPoint{130.f, 65.f});
    overlay->addChild(panel);

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.create_tag", "Create Custom Tag").c_str(),
        "bigFont.fnt"
    );
    titleLbl->setScale(0.4f);
    titleLbl->setPosition({winSize.width / 2, winSize.height / 2 + 40});
    overlay->addChild(titleLbl, 1);

    m_newTagInput = TextInput::create(180, tr("pai.hub.forum.new_tag", "Tag name"));
    m_newTagInput->setPosition(winSize / 2.f);
    m_newTagInput->setScale(0.6f);
    overlay->addChild(m_newTagInput, 51);

    auto btnMenu = makeZeroMenu("create-tag-menu"_spr);
    btnMenu->setContentSize(winSize);
    overlay->addChild(btnMenu, 52);

    auto closeSpr = paimon::ui::makeButtonFace("x", {24.f, 24.f});
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(PaimonHubLayer::onCloseCreateTag));
    closeBtn->setPosition({winSize.width / 2 - 130.f + 6.f, winSize.height / 2 + 65.f - 6.f});
    btnMenu->addChild(closeBtn);

    auto createSpr = paimon::ui::makeButtonFace(tr("pai.hub.forum.create", "Create").c_str(),
        {96.f, 26.f}, paimon::ui::actionColor("GJ_button_01.png"));
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
            "bigFont.fnt"
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
            auto chip = makePillFace(t.c_str(), paimon::ui::palette::raised, 0.18f);
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
                pre->setColor(paimon::ui::palette::muted);
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

        paimon::fluid::RevealOpts reveal;
        reveal.startDelay = 0.02f * i;
        paimon::fluid::revealNode(cardContainer, reveal);

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
        auto tagSpr = makePillFace(label.c_str(), isSelected ? ccColor3B{36, 75, 106}
            : paimon::ui::palette::raised, 0.32f);
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
        setPillSelected(m_sortBtns[i], idx == static_cast<int>(i));
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

    auto* panel = paimon::ui::makeSurface({panelW, panelH});
    panel->setPosition({panelX, panelY});
    m_predefPickerOverlay->addChild(panel);

    auto titleLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.predef.title", "Pick Predefined Tags").c_str(),
        "bigFont.fnt"
    );
    titleLbl->setScale(0.4f);
    titleLbl->setPosition({winSize.width / 2.f, panelY + panelH - 20.f});
    m_predefPickerOverlay->addChild(titleLbl, 1);

    auto hintLbl = CCLabelBMFont::create(
        tr("pai.hub.forum.predef.hint", "Tap to enable/disable").c_str(),
        "bigFont.fnt"
    );
    hintLbl->setScale(0.3f);
    hintLbl->setColor(paimon::ui::palette::muted);
    hintLbl->setPosition({winSize.width / 2.f, panelY + panelH - 40.f});
    m_predefPickerOverlay->addChild(hintLbl, 1);

    auto closeMenu = makeZeroMenu("predef-picker-close"_spr);
    closeMenu->setContentSize(winSize);
    m_predefPickerOverlay->addChild(closeMenu, 52);

    auto closeSpr = paimon::ui::makeButtonFace("x", {24.f, 24.f});
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

        auto chipSpr = makePillFace(m_forumTags[i].c_str(), isActive ? ccColor3B{36, 75, 106}
            : paimon::ui::palette::raised, 0.34f);
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
        setPillSelected(btn, contains(m_activePredefTags, tag));
    }
    refreshTagButtons();
}

void PaimonHubLayer::onForumSubTabSwitch(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    switchForumSubTab(idx);
}

void PaimonHubLayer::switchForumSubTab(int idx) {
    m_forumSubTab = idx;

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
        setPillSelected(btn, !wasSelected);
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
