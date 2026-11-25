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
#include "../features/keybinds/ui/ManagerCutPopup.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../features/cursor/ui/CursorConfigPopup.hpp"
#include "../features/smooth-scroll/ui/SmoothScrollConfigPopup.hpp"
#endif
#include "../features/pet/ui/PetConfigPopup.hpp"
#include "../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../features/progressbar/ui/ProgressBarConfigPopup.hpp"
#include "../features/liquid-glass/services/GlassDecorator.hpp"
#include "../features/liquid-glass/ui/GlassConfigPopup.hpp"
#include "../features/dynamic-ambient/services/AmbientDecorator.hpp"
#include "../features/dynamic-ambient/ui/AmbientConfigPopup.hpp"
#include "../features/custom-slider/ui/CustomSliderPopup.hpp"
#include "../features/discord-presence/ui/DiscordConfigPopup.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/beat-shaders/ui/BeatShaderConfigLayer.hpp"
#include "../features/rtx/ui/RTXConfigLayer.hpp"
#include "../features/paimon-shop/ui/PaimonShopLayer.hpp"
#include "../features/dynamic-songs/ui/DynamicSongPopup.hpp"
#include "../features/dynamic-volume/ui/DynamicVolumePopup.hpp"
#include "../features/settings-panel/services/SettingsPanelManager.hpp"
#include "../features/settings-panel/ui/SettingsCategoryBuilder.hpp"
#include "../features/settings-panel/ui/SettingsControls.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/forum/ui/ForumPanel.hpp"
#include "../features/dev-tools/ui/GifToSheetPopup.hpp"
#include "../features/thumb-requests/ui/ThumbRequestsPopup.hpp"
#include "../core/ModAuthFlow.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../features/updates/ui/UpdateProgressPopup.hpp"
#include "../features/updates/ui/UpdateCenterPopup.hpp"
#include "../ui/FeatureInfoPopup.hpp"
#include "../ui/FeatureConfigPopup.hpp"
#include "../ui/SmoothUIConfigPopup.hpp"
#include "../ui/HubFeatureInfo.hpp"
#include "../utils/PaimonNotification.hpp"
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
#include "../features/community-base/ui/CommunityBasePopup.hpp"

using namespace geode::prelude;

namespace {
using paimon::loc::tr;



constexpr float kHomeBottom = 15.f;
constexpr float kHomeSidebarX = 15.f;
constexpr float kHomeSidebarW = 120.f;
constexpr float kHomeDetailsX = kHomeSidebarX + kHomeSidebarW + 8.f;
constexpr float kHomeContentX = kHomeDetailsX + 12.f;
constexpr float kHomeSearchDrop = 40.f;
constexpr float kHomeFilterDrop = 66.f;
constexpr float kHomeListDrop = 86.f;

float homeDetailsW(CCSize const& win) { return win.width - kHomeDetailsX - 15.f; }
float homeInnerW(CCSize const& win) { return homeDetailsW(win) - 24.f; }
float homeTop(CCSize const& win) { return win.height - 70.f; }

void animateActionCard(CCMenuItemSpriteExtra* button, float x, float y, float delay) {
    button->setPosition({x, y});
    paimon::ui::animateIn(button, delay);
}

CCSprite* makeCategoryIcon(std::string const& frame, float size) {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame.empty() ? "GJ_starsIcon_001.png" : frame.c_str());
    if (!icon) return nullptr;
    auto const box = icon->getContentSize();
    icon->setScale(size / std::max(1.f, std::max(box.width, box.height)));
    return icon;
}

CCMenuItemSpriteExtra* makeActionCardBtn(
    paimon::hubdata::HubActionMeta const& action,
    std::string const& iconFrame,
    float width, float height,
    WeakRef<PaimonHubLayer> self
) {
    constexpr float kIcon = 16.f;
    constexpr float kDescScale = 0.42f;
    auto* face = CCNode::create();
    face->setContentSize({width, height});
    face->setAnchorPoint({0.5f, 0.5f});
    face->addChild(paimon::ui::makeInset({width, height}, 85), -1);
    float const headY = height - 7.f - kIcon / 2.f;
    float textX = 10.f;
    if (auto* icon = makeCategoryIcon(iconFrame, kIcon)) {
        icon->setPosition({8.f + kIcon / 2.f, headY});
        face->addChild(icon);
        textX = 13.f + kIcon;
    }
    auto* title = CCLabelBMFont::create(action.title.c_str(), "bigFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->setColor(paimon::ui::palette::text);
    title->limitLabelWidth(width - textX - 24.f, 0.32f, 0.14f);
    title->setPosition({textX, headY});
    face->addChild(title);
    auto* desc = CCLabelBMFont::create(action.desc.c_str(), "chatFont.fnt",
        (width - 20.f) / kDescScale, kCCTextAlignmentLeft);
    desc->setScale(kDescScale);
    desc->setColor(paimon::ui::palette::muted);
    desc->setAnchorPoint({0.f, 1.f});
    desc->setPosition({10.f, height - 11.f - kIcon});
    face->addChild(desc);

    auto* favoriteMenu = CCMenu::create();
    favoriteMenu->setPosition({0.f, 0.f});
    favoriteMenu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    if (star && !action.id.empty()) {
        auto favorites = Mod::get()->getSavedValue<std::vector<std::string>>("hub-favorites", {});
        bool const favorite = std::find(favorites.begin(), favorites.end(), action.id) != favorites.end();
        star->setColor(favorite ? ccColor3B{255, 255, 255} : ccColor3B{90, 90, 90});
        star->setOpacity(favorite ? 255 : 170);
        auto* pin = CCMenuItemExt::createSpriteExtra(star, [self, id = action.id](CCMenuItemSpriteExtra*) {
            if (auto hub = self.lock()) hub->toggleHubFavorite(id);
        });
        pin->setScale(0.36f);
        pin->m_baseScale = 0.36f;
        pin->setPosition({width - 13.f, headY});
        favoriteMenu->addChild(pin);
    }
    face->addChild(favoriteMenu, 5);
    auto* button = CCMenuItemExt::createSpriteExtra(face, [self, action](CCMenuItemSpriteExtra*) {
        if (auto hub = self.lock(); hub && hub->getParent()) {
            hub->activateHubAction(action.id, action.onPress);
        }
    });
    button->m_scaleMultiplier = 1.04f;
    return button;
}
template <typename T>
bool contains(std::vector<T> const& v, T const& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

} // namespace

namespace paimon::hubdata {

// mobile lacks the setting; hassetting is the safe check.
bool discordSupported() {
    auto* mod = geode::Mod::get();
    return mod && mod->hasSetting("discord-rpc-enabled");
}

std::vector<HubCategoryMeta> const& getHubCategories() {
    // membership only depends on load-time setting registration; build once.
    static std::vector<HubCategoryMeta> const cats = [] {
        std::vector<HubCategoryMeta> result = {
            {"General", "Idioma, updates y mantenimiento.", {130, 240, 170}, paimon::ui::getGeneralInfo, "GJ_optionsBtn02_001.png"},
            {"Miniaturas", "Layout, galeria, efectos y captura.", {120, 210, 255}, paimon::ui::getThumbnailsInfo, "GJ_extendedIcon_001.png"},
            {"Nivel", "Pantalla de info, fondo y transiciones.", {170, 190, 255}, paimon::ui::getLevelInfoScreenInfo, "GJ_starsIcon_001.png"},
            {"Audio", "Profile music, menu music y capas.", {255, 165, 210}, paimon::ui::getAudioInfo, "GJ_musicIcon_001.png"},
            {"Fondos", "Fondos por capa, video y transiciones.", {140, 245, 200}, paimon::ui::getBackgroundsInfo, "GJ_sMagicIcon_001.png"},
            {"Extras", "Mascota, cursor, popups y rendimiento.", {255, 140, 140}, paimon::ui::getExtrasInfo, "GJ_diamondsIcon_001.png"},
        };
        if (discordSupported()) {
            result.push_back({"Discord", "Rich Presence completa.", {140, 160, 255}, paimon::ui::getDiscordInfo, "gj_discordIcon_001.png"});
        }
        result.push_back({"Crear", "Iconos, texturas y recursos propios.", {182, 161, 255}, [] {
            return std::vector<paimon::ui::InfoSection>{
                {"Iconos", "Crea, importa y organiza tus iconos. La galeria conserva proyectos y favoritos.", {182, 161, 255}},
                {"Texturas", "Organiza paquetes y edita sus colores con una vista previa en el juego.", {116, 204, 255}},
                {"GIF a Sheet", "Convierte una animacion en una hoja de sprites para tus recursos.", {255, 208, 128}}
            };
        }, "GJ_paintBtn_001.png"});
        return result;
    }();
    return cats;
}

std::vector<HubActionMeta> getHubActions(int categoryIndex) {
    // the creation category follows the optional Discord category.
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
                {"Mi Cuenta", "GJ_button_01.png", [](PaimonHubLayer*) {
                    paimon::modauth::showPanel();
                }, 0, "Inicia sesion unificada y consulta tus permisos"},
                {"Modulos", "GJ_button_03.png", [](PaimonHubLayer*) {
                    auto scene = PaimonModulesLayer::scene();
                    if (scene) CCDirector::get()->pushScene(scene);
                }, 0, "Activa o desactiva funciones"},
                {"Configurar", "GJ_button_01.png", [](PaimonHubLayer*) { SettingsPanelManager::get().open(0); }, 0, "Idioma, updates y basicos"},
                {"Manager Cut", "GJ_button_04.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::keybinds::ManagerCutPopup::create()) popup->show();
                }, 0, "Teclas, atajos y gestos"},
                {"Actualizaciones", "GJ_button_02.png", [](PaimonHubLayer*) {
                    if (auto popup = paimon::updates::UpdateCenterPopup::create()) popup->show();
                }, 0, "Nueva version y versiones antiguas"},
                {"Comunidad", "GJ_button_02.png", [](PaimonHubLayer*) {
                    if (auto* scene = CommunityHubLayer::scene()) CCDirector::get()->pushScene(scene);
                }, 0, "Creadores, miniaturas y mods compatibles"},
                {"Paimon Base", "GJ_button_01.png", [](PaimonHubLayer*) {
                    paimon::community::CommunityBasePopup::open();
                }, 0, "Ajustes en la nube y sugerencias"},
                {"Paimon Shop", "GJ_button_05.png", [](PaimonHubLayer*) {
                    paimon::shop::PaimonShopLayer::open();
                }, 0, "Presets de layout, RTX y shaders con preview"},
                {"Versus", "GJ_button_05.png", [](PaimonHubLayer*) {
                    if (auto* scene = paimon::versus::VersusHubLayer::scene()) CCDirector::get()->pushScene(scene);
                }, 0, "Partidas, mazos y clasificaciones"},
                {"Peticiones en directo", "GJ_button_02.png", [](PaimonHubLayer*) {
                    paimon::twitch::TwitchRequestsLayer::open();
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
                        PaimonNotify::show("Necesitas iniciar sesion.", NotificationIcon::Warning);
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
                {"Liquid Glass", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = paimon::glass::GlassConfigPopup::create()) popup->show(); }, 4, "Popups y botones de cristal"},
                {"Dynamic Ambient", "GJ_button_03.png", [](PaimonHubLayer*) { if (auto popup = paimon::ambient::AmbientConfigPopup::create()) popup->show(); }, 4, "Luz dinamica segun el fondo"},
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
#if defined(GEODE_IS_DESKTOP)
                {"Cursor", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = CursorConfigPopup::create()) popup->show(); }, 5, "Cursor personalizado"},
#endif
                {"Hover", "GJ_button_01.png", [](PaimonHubLayer*) { paimon::hover::open(); }, 5, "Animaciones al pasar cursor o touch"},
                {"Slider", "GJ_button_01.png", [](PaimonHubLayer*) { if (auto popup = paimon::slider::CustomSliderPopup::create()) popup->show(); }, 5, "Barra de scroll custom"},
#if defined(GEODE_IS_DESKTOP)
                {"Scroll", "GJ_button_02.png", [](PaimonHubLayer*) { if (auto popup = paimon::smoothscroll::SmoothScrollConfigPopup::create()) popup->show(); }, 5, "Desplazamiento suave"},
#endif
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
                {"Refrescar", "GJ_button_05.png", [](PaimonHubLayer*) { paimon::discord::DiscordPresenceManager::get().refreshNow(true); PaimonNotify::show("Actualizacion de Rich Presence solicitada.", NotificationIcon::Info); }, 6, "Solicita una actualizacion"},
            };
        default:
            return {};
    }
}

std::vector<GranularSettingMeta> const& getGranularSettings() {
    // static catalog; was rebuilt on every search keystroke.
    static std::vector<GranularSettingMeta> const settings = [] {
        std::vector<GranularSettingMeta> result = {
        {"Language / Idioma", "Idioma / Language", 0},
        {"Manager Cut (Keybinds & Gestures)", "Manager Cut (Teclas y Gestos)", 0},
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
        {"Notification Animation", "Animacion de la Notificacion", 3},
        {"Notification Animation Speed", "Velocidad de la Animacion de Notificacion", 3},
        {"Notification Animation Intensity", "Intensidad de la Animacion de Notificacion", 3},
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
#if defined(GEODE_IS_DESKTOP)
        {"Custom Cursor", "Cursor Personalizado", 5},
        {"Cursor Trail", "Estela de Cursor", 5},
        {"Cursor Scale", "Escala de Cursor", 5},
#endif
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
            result.push_back({"Enable Discord Rich Presence", "Activar Discord Rich Presence", 6});
            result.push_back({"Configure Discord RPC", "Configurar Discord RPC", 6});
            result.push_back({"Refresh Discord Status", "Refrescar Estado de Discord", 6});
        }
        return result;
    }();
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
    paimon::ui::detachGeodeTextInput(m_searchInput);
}

bool PaimonHubLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->setMouseEnabled(true);
    this->scheduleUpdate();

    m_gdMode = Mod::get()->getSavedValue<std::string>("hub-ui-style", "original") == "gd";
    if (m_gdMode) {
        buildGDShell();
        return true;
    }

    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2;
    float top = winSize.height;

    paimon::ui::decorateScene(this);

    auto* window = NineSlice::create("GJ_square01.png");
    window->setContentSize({winSize.width - 16.f, winSize.height - 44.f});
    window->setAnchorPoint({0.f, 0.f});
    window->setPosition({8.f, 8.f});
    window->setID("paimon-hub-window"_spr);
    this->addChild(window, 1);
    paimon::ui::addCorners(window, window->getContentSize(), SideArtStyle::PopupGold, 1.f);

    m_mainMenu = paimon::ui::makeZeroMenu("paimon-hub-main-menu"_spr);
    this->addChild(m_mainMenu, 10);

    bool const narrow = winSize.width < 500.f;
    auto title = CCLabelBMFont::create(tr("pai.hub.title", "Paimbnails").c_str(), "goldFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({40.f, top - 18.f});
    title->limitLabelWidth(narrow ? 84.f : 110.f, 0.7f, 0.3f);
    this->addChild(title, 10);

    auto backBtn = paimon::ui::makeBackButton([this] { onBack(nullptr); });
    backBtn->setPosition({20.f, top - 18.f});
    backBtn->setScale(0.62f);
    backBtn->m_baseScale = 0.62f;
    m_mainMenu->addChild(backBtn);

    auto* helpBtn = paimon::ui::makeFrameButton("GJ_infoIcon_001.png", 0.7f, [this] { onOpenHelp(nullptr); });
    helpBtn->setID("help-btn"_spr);
    helpBtn->setPosition({winSize.width - 20.f, top - 18.f});
    m_mainMenu->addChild(helpBtn);

    auto* uiBtn = paimon::ui::makeButton("GD", [this] { onToggleUIStyle(nullptr); },
        paimon::ui::Btn::Gray, 0.f, 0.5f);
    uiBtn->setID("ui-style-btn"_spr);
    uiBtn->setPosition({winSize.width - 50.f, top - 18.f});
    m_mainMenu->addChild(uiBtn);

    auto* updBtn = paimon::ui::makeButton(tr("pai.hub.btn.updates", "Updates").c_str(),
        [this] { onCheckUpdate(nullptr); },
        paimon::ui::Btn::Cyan, narrow ? 54.f : 68.f, 0.5f);
    updBtn->setID("updates-btn"_spr);
    updBtn->setPosition({winSize.width - (narrow ? 102.f : 110.f), top - 18.f});
    m_mainMenu->addChild(updBtn);

    float tabY = top - 18.f;
    std::vector<std::string> tabNames = {
        tr("pai.hub.tab.home", "Home"),
        tr("pai.hub.tab.forum", "Forum")
    };

    auto tabBar = CCMenu::create();
    tabBar->setID("paimon-hub-tab-bar"_spr);
    tabBar->setPosition({cx + (winSize.width < 500.f ? 2.f : 4.f), tabY});
    tabBar->setContentSize({130.f, 20.f});
    tabBar->setAnchorPoint({0.5f, 0.5f});
    tabBar->setLayout(
        RowLayout::create()
            ->setGap(6.f)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    this->addChild(tabBar, 10);

    static char const* kTabIds[] = {"home-tab-btn"_spr, "forum-tab-btn"_spr};
    for (int i = 0; i < 2; i++) {
        auto* btn = paimon::ui::makeButton(tabNames[i].c_str(), [this, i] { switchTab(i); },
            paimon::ui::Btn::Gray, 60.f, 0.5f);
        btn->setTag(i);
        btn->setID(kTabIds[i]);
        tabBar->addChild(btn);
        m_tabBtns.push_back(btn);
    }
    tabBar->updateLayout();

    auto addTab = [this](CCLayerRGBA*& tab, CCMenu*& menu, char const* tabId, char const* menuId, bool visible) {
        tab = cocos2d::CCLayerRGBA::create();
        tab->setID(tabId);
        tab->setCascadeOpacityEnabled(false);
        tab->setVisible(visible);
        this->addChild(tab, 5);
        menu = paimon::ui::makeZeroMenu(menuId);
        menu->setVisible(visible);
        this->addChild(menu, 11);
    };
    addTab(m_homeTab, m_homeMenu, "home-tab"_spr, "home-menu"_spr, true);
    addTab(m_forumTab, m_forumMenu, "forum-tab"_spr, "forum-menu"_spr, false);

    buildHomeTab();
    buildForumTab();
    setupGlassChrome();
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

void PaimonHubLayer::setupGlassChrome() {
    // buttons get their plates from the global ButtonSprite hook, and a
    // skin change (selected tab or category) re-tints them. Only the
    // panels need plates here. Plates stay inert while the material
    // or its "Popups & panels" scope is off.
    if (m_gdMode) return;
    paimon::glass::attachGlass(m_sidebarBg, paimon::glass::GlassKind::Panel, paimon::glass::GlassScope::Popups);
    paimon::glass::attachGlass(m_detailsBg, paimon::glass::GlassKind::Panel, paimon::glass::GlassScope::Popups);
    if (m_forumPanel) m_forumPanel->setupGlassChrome();
}

void PaimonHubLayer::switchTab(int idx) {
    idx = (idx == 1) ? 1 : 0;
    int prev = m_currentTab;
    m_currentTab = idx;

    m_homeTab->setVisible(idx == 0);
    m_homeMenu->setVisible(idx == 0);
    m_forumTab->setVisible(idx == 1);
    m_forumMenu->setVisible(idx == 1);

    auto* activeTab = idx == 0 ? m_homeTab : m_forumTab;
    auto* activeMenu = idx == 0 ? m_homeMenu : m_forumMenu;
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
    for (size_t i = 0; i < m_tabBtns.size(); ++i) {
        if (m_gdMode) {
            if (auto* sprite = typeinfo_cast<ButtonSprite*>(m_tabBtns[i]->getNormalImage())) {
                sprite->setColor(static_cast<int>(i) == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
            }
        } else {
            paimon::ui::setButtonSkin(m_tabBtns[i], static_cast<int>(i) == idx ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
        }
    }
    if (!m_gdMode) paimon::ui::matchButtonLabels(m_tabBtns);
    if (idx == 1 && m_forumPanel) m_forumPanel->onTabActivated();
}

void PaimonHubLayer::buildHomeTab() {
    auto const win = CCDirector::get()->getWinSize();
    float const panelH = win.height - 58.f;
    float const panelTop = kHomeBottom + panelH;
    float const top = homeTop(win);
    float const sideCx = kHomeSidebarX + kHomeSidebarW / 2.f;
    auto categories = getHubCategories();
    m_sidebarBg = paimon::ui::makeInset({kHomeSidebarW, panelH}, 80);
    m_sidebarBg->setPosition({kHomeSidebarX, kHomeBottom});
    m_homeTab->addChild(m_sidebarBg);
    m_sidebarMenu = paimon::ui::makeZeroMenu("paimon-sidebar-menu"_spr);
    m_homeTab->addChild(m_sidebarMenu, 3);

    // quick Hub and the version label own the bottom 50 units of the sidebar.
    float const firstY = panelTop - 16.f;
    float const listSpan = firstY - (kHomeBottom + 50.f);
    float const spacing = std::min(23.f,
        listSpan / std::max(1.f, static_cast<float>(categories.size() - 1)));
    float const buttonScale = std::clamp(spacing / 30.f * 0.82f, 0.45f, 0.6f);
    m_homeCategoryBtns.clear();
    for (size_t i = 0; i < categories.size(); ++i) {
        auto* button = paimon::ui::makeButton(categories[i].title.c_str(),
            [this, idx = static_cast<int>(i)] { switchHomeCategory(idx); },
            paimon::ui::Btn::Gray, kHomeSidebarW - 16.f, buttonScale, "bigFont.fnt");
        button->setPosition({sideCx, firstY - static_cast<float>(i) * spacing});
        button->setTag(100 + static_cast<int>(i));
        m_sidebarMenu->addChild(button);
        m_homeCategoryBtns.push_back(button);
    }
    auto* quick = paimon::ui::makeButton("Quick Hub", [] {
        if (auto* popup = paimon::quickhub::RadialConfigPopup::create()) popup->show();
    }, paimon::ui::Btn::Pink, kHomeSidebarW - 28.f, 0.55f);
    quick->setPosition({sideCx, kHomeBottom + 30.f});
    m_sidebarMenu->addChild(quick);
    auto* version = CCLabelBMFont::create(fmt::format("v{}", paimon::updates::UpdateChecker::get().localVersion()).c_str(), "chatFont.fnt");
    version->setScale(0.4f);
    version->setColor(paimon::ui::palette::muted);
    version->setPosition({sideCx, kHomeBottom + 11.f});
    m_homeTab->addChild(version, 2);

    float const detailsW = homeDetailsW(win);
    float const innerW = homeInnerW(win);
    float const rightX = kHomeContentX + innerW;
    m_detailsBg = paimon::ui::makeInset({detailsW, panelH}, 45);
    m_detailsBg->setPosition({kHomeDetailsX, kHomeBottom});
    m_homeTab->addChild(m_detailsBg);
    m_homeCategoryTitle = CCLabelBMFont::create("", "goldFont.fnt");
    m_homeCategoryTitle->setAnchorPoint({0.f, 0.5f});
    m_homeCategoryTitle->setPosition({kHomeContentX, top});
    m_homeTab->addChild(m_homeCategoryTitle, 2);
    m_homeCategoryDesc = CCLabelBMFont::create("", "chatFont.fnt");
    m_homeCategoryDesc->setAnchorPoint({0.f, 0.5f});
    m_homeCategoryDesc->setColor(paimon::ui::palette::muted);
    m_homeCategoryDesc->setPosition({kHomeContentX, top - 17.f});
    m_homeTab->addChild(m_homeCategoryDesc, 2);

    auto* infoSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
    if (infoSprite) {
        m_homeCategoryInfoBtn = CCMenuItemExt::createSpriteExtra(infoSprite, [self = WeakRef<PaimonHubLayer>(this)](CCMenuItemSpriteExtra*) {
            if (auto hub = self.lock()) {
                auto cats = getHubCategories();
                if (auto* popup = paimon::ui::FeatureInfoPopup::create(cats[hub->m_homeSelectedCategory].title,
                    cats[hub->m_homeSelectedCategory].getInfo())) popup->show();
            }
        });
        m_homeCategoryInfoBtn->setScale(0.55f);
        m_homeCategoryInfoBtn->m_baseScale = 0.55f;
        m_homeCategoryInfoBtn->setPosition({rightX - 8.f, top});
        m_sidebarMenu->addChild(m_homeCategoryInfoBtn);
    }
    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    constexpr float kSearchScale = 0.8f;
    float const searchW = innerW - 26.f;
    float const searchY = top - kHomeSearchDrop;
    m_searchInput = TextInput::create(searchW / kSearchScale,
        es ? "Buscar funciones y ajustes..." : "Find features and settings...", "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Text);
    m_searchInput->setMaxCharCount(64);
    m_searchInput->setScale(kSearchScale);
    m_searchInput->setPosition({kHomeContentX + searchW / 2.f, searchY});
    m_searchInput->setCallback([self = WeakRef<PaimonHubLayer>(this)](std::string const&) {
        if (auto hub = self.lock(); hub && hub->getParent()) hub->rebuildHomeCategoryCards();
    });
    m_homeTab->addChild(m_searchInput, 10);
    auto* clear = paimon::ui::makeFrameButton("GJ_deleteIcon_001.png", 0.45f, [this] {
        m_searchInput->setString("");
        rebuildHomeCategoryCards();
    });
    clear->setPosition({rightX - 8.f, searchY});
    m_sidebarMenu->addChild(clear);
    buildHomeFilterBar();
    m_homeActionsMenu = paimon::ui::makeZeroMenu();
    m_homeMenu->addChild(m_homeActionsMenu);
    switchHomeCategory(std::clamp(Mod::get()->getSavedValue<int>("hub-last-category", 0),
        0, static_cast<int>(categories.size()) - 1));
}

void PaimonHubLayer::buildHomeFilterBar() {
    if (m_homeFilterBar) m_homeFilterBar->removeFromParent();
    auto const win = CCDirector::get()->getWinSize();
    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    m_homeFilterBar = paimon::configkit::makeTabBar(homeInnerW(win),
        {es ? "Categoria" : "Category", es ? "Favoritos" : "Favorites", es ? "Recientes" : "Recent"},
        m_homeFilter, [this](int filter) {
            m_homeFilter = filter;
            rebuildHomeCategoryCards();
        });
    m_homeFilterBar->setPosition({kHomeContentX,
        homeTop(win) - kHomeFilterDrop - paimon::configkit::kTabBarHeight / 2.f});
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
    for (size_t i = 0; i < m_homeCategoryBtns.size(); ++i) {
        paimon::ui::setButtonSkin(m_homeCategoryBtns[i],
            static_cast<int>(i) == m_homeSelectedCategory ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(m_homeCategoryBtns);
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

bool PaimonHubLayer::scrollWheel(float x, float y) {
    if (m_currentTab == 0 && paimon::configkit::queueWheelScroll(m_homeActionsScroll,
        x, y, m_homeScrollTarget, m_homeScrollTargetSet)) return true;
    if (m_currentTab == 1 && m_forumPanel && m_forumPanel->handleScrollWheel(x, y)) return true;
    return CCLayer::scrollWheel(x, y);
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
#if defined(GEODE_IS_DESKTOP)
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
#else
    (void)layoutKeybind;
    std::string body = es
        ? "Busca funciones por nombre, descripcion o ajuste.\n\n"
          "Toca la estrella para guardar un favorito. Recientes muestra tus ultimos doce accesos.\n\n"
          "Pulsa el valor de un selector para buscar entre sus opciones.\n\n"
          "Configura tus <cy>gestos tactiles</c> desde <cg>General -> Manager Cut</c>."
        : "Find features by name, description or setting.\n\n"
          "Tap a star to save a favorite. Recent shows your last twelve visits.\n\n"
          "Tap a selector value to search its options.\n\n"
          "Customize <cy>touch gestures</c> in <cg>General -> Manager Cut</c>.";
#endif

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
    std::string const query = geode::utils::string::toLower(m_searchInput ? m_searchInput->getString() : "");
    auto favorites = Mod::get()->getSavedValue<std::vector<std::string>>("hub-favorites", {});
    auto recent = Mod::get()->getSavedValue<std::vector<std::string>>("hub-recent", {});
    bool const es = Localization::get().getLanguage() == Localization::Language::SPANISH;
    std::vector<HubActionMeta> actions;
    auto include = [&](HubActionMeta action, int categoryIndex) {
        action.categoryIndex = categoryIndex;
        if (m_homeFilter == 1 && !contains(favorites, action.id)) return;
        if (m_homeFilter == 2 && !contains(recent, action.id)) return;
        if (!query.empty() && geode::utils::string::toLower(action.title + " " + action.desc + " " + categories[categoryIndex].title).find(query) == std::string::npos) return;
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
    float const detailsW = homeDetailsW(win);
    m_homeCategoryTitle->setString(title.c_str());
    m_homeCategoryTitle->limitLabelWidth(detailsW - 58.f, 0.62f, 0.3f);
    m_homeCategoryDesc->setString((query.empty() && m_homeFilter == 0 ? category.shortDesc
        : fmt::format(fmt::runtime(es ? "{} funciones disponibles" : "{} features available"), actions.size())).c_str());
    m_homeCategoryDesc->limitLabelWidth(detailsW - 28.f, 0.42f, 0.20f);
    if (m_homeCategoryInfoBtn) m_homeCategoryInfoBtn->setVisible(query.empty() && m_homeFilter == 0);

    float const scrollW = homeInnerW(win);
    float const scrollY = kHomeBottom + 8.f;
    float const scrollH = std::max(40.f, homeTop(win) - kHomeListDrop - scrollY);
    m_homeActionsScroll = ScrollLayer::create({scrollW, scrollH});
    if (m_homeActionsScroll) m_homeActionsScroll->setAnchorPoint({0.f, 0.f});
    m_homeActionsScroll->setPosition({kHomeContentX, scrollY});
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
    constexpr float kCardGap = 6.f;
    int const columns = std::max(1, static_cast<int>((scrollW + kCardGap) / 160.f));
    float const cardW = (scrollW - kCardGap * (columns - 1)) / columns;
    float cardH = 44.f;
    for (auto const& action : actions) {
        auto* label = CCLabelBMFont::create(action.desc.c_str(), "chatFont.fnt",
            (cardW - 20.f) / 0.42f, kCCTextAlignmentLeft);
        cardH = std::max(cardH, 34.f + label->getContentSize().height * 0.42f);
    }
    int const rows = (static_cast<int>(actions.size()) + columns - 1) / columns;
    float const contentH = std::max(scrollH, rows * (cardH + kCardGap));
    content->setContentSize({scrollW, contentH});
    auto* menu = paimon::ui::makeZeroMenu();
    content->addChild(menu);
    for (size_t i = 0; i < actions.size(); ++i) {
        int const column = static_cast<int>(i) % columns;
        int const row = static_cast<int>(i) / columns;
        auto* button = makeActionCardBtn(actions[i], categories[actions[i].categoryIndex].icon,
            cardW, cardH, WeakRef<PaimonHubLayer>(this));
        menu->addChild(button);
        CCPoint const position{cardW / 2.f + column * (cardW + kCardGap),
            contentH - cardH / 2.f - row * (cardH + kCardGap)};
        if (resetScroll) animateActionCard(button, position.x, position.y, 0.02f * static_cast<float>(i));
        else button->setPosition(position);
    }
    if (resetScroll) m_homeActionsScroll->moveToTop();
    else content->setPositionY(std::clamp(scrollH - contentH + previousOffset, scrollH - contentH, 0.f));
}

void PaimonHubLayer::buildForumTab() {
    auto const winSize = CCDirector::get()->getWinSize();
    CCSize const panelSize{winSize.width - 30.f, winSize.height - 58.f};
    m_forumPanel = paimon::forum::ForumPanel::create(panelSize, m_gdMode);
    if (!m_forumPanel) return;
    m_forumPanel->setPosition({15.f, 15.f});
    m_forumTab->addChild(m_forumPanel, 2);
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
        auto* mark = CCSprite::createWithSpriteFrameName("exMark_001.png");
        float const parentScale = std::max(0.1f, btn->getScale());
        float const baseScale = 0.42f / parentScale;
        float const pulseScale = 0.48f / parentScale;
        float const inset = 3.f / parentScale;
        mark->setScale(baseScale);
        mark->setID("updates-btn-dot"_spr);
        mark->setAnchorPoint({0.5f, 0.5f});
        auto size = btn->getContentSize();
        mark->setPosition({size.width - inset, size.height - inset});
        btn->addChild(mark, 100);
        if (paimon::ui::motionEnabled()) mark->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(0.45f, pulseScale),
            CCScaleTo::create(0.45f, baseScale),
            nullptr
        )));
    } else if (!wanted && dot) {
        dot->removeFromParent();
    }
}
