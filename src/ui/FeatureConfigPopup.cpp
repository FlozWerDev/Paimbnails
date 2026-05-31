#include "FeatureConfigPopup.hpp"

#include "../features/settings-panel/services/SettingsPanelManager.hpp"
#include "../features/settings-panel/ui/SettingsControls.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../features/cursor/ui/CursorConfigPopup.hpp"
#include "../features/pet/ui/PetConfigPopup.hpp"
#include "../features/progressbar/ui/ProgressBarConfigPopup.hpp"
#include "../features/custom-slider/ui/CustomSliderPopup.hpp"
#include "../features/discord-presence/ui/DiscordConfigPopup.hpp"
#include "../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../layers/PaiConfigLayer.hpp"
#include "../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/GJAccountManager.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace cocos2d;
using namespace geode::prelude;
using namespace paimon::settings_ui;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers para acceso tipado a settings y saved values
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
T gset(const char* key) {
    if (Mod::get()->hasSetting(key)) return Mod::get()->getSettingValue<T>(key);
    return Mod::get()->getSavedValue<T>(key, T{});
}

template<typename T>
void sset(const char* key, T val) {
    if (Mod::get()->hasSetting(key)) Mod::get()->setSettingValue<T>(key, val);
    else Mod::get()->setSavedValue(key, val);
}

template<typename T>
T gsaved(const char* key, T def) {
    return Mod::get()->getSavedValue<T>(key, def);
}

template<typename T>
void ssaved(const char* key, T val) {
    Mod::get()->setSavedValue(key, val);
}

void openNativeSettings() {
    geode::openSettingsPopup(Mod::get(), false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tipos del registro
// ─────────────────────────────────────────────────────────────────────────────

struct FeatureGroup {
    std::string title;
    std::string subtitle;
    // builder agrega filas a 'container' con el ancho 'width'
    std::function<void(CCNode* container, float width)> build;
};

// ─────────────────────────────────────────────────────────────────────────────
// Builders por grupo
// ─────────────────────────────────────────────────────────────────────────────

void buildGeneralGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Idioma & Logs", w));

    c->addChild(createDropdownRow("Language",
        gset<std::string>("language"),
        {"english", "spanish", "portuguese", "french", "german", "russian", "japanese"},
        [](std::string const& v) { sset<std::string>("language", v); },
        w));

    c->addChild(createToggleRow("Auto Update",
        gset<bool>("auto-update"),
        [](bool v) { sset<bool>("auto-update", v); },
        w));

    c->addChild(createToggleRow("Debug Logs",
        gset<bool>("enable-debug-logs"),
        [](bool v) { sset<bool>("enable-debug-logs", v); },
        w));

    c->addChild(createSectionHeader("Busqueda Rapida", w));

    c->addChild(createToggleRow("Realtime Search Preview",
        gset<bool>("realtime-search-preview"),
        [](bool v) { sset<bool>("realtime-search-preview", v); },
        w));

    c->addChild(createLinkRow("Configurar Quick Search Key (Geode)",
        []() { openNativeSettings(); },
        w));

    c->addChild(createSectionHeader("Atajos", w));

    c->addChild(createLinkRow("Settings Panel Keybind",
        []() { openNativeSettings(); },
        w));

    c->addChild(createLinkRow("Layout Editor Keybind",
        []() { openNativeSettings(); },
        w));
}

void buildThumbnailLayoutGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Tamano y Fondo de Celda", w));

    c->addChild(createSliderRow("Thumbnail Size",
        static_cast<float>(gset<double>("level-thumb-width")),
        0.2f, 0.95f,
        [](float v) { sset<double>("level-thumb-width", static_cast<double>(v)); },
        w));

    c->addChild(createDropdownRow("Background Style",
        gsaved<std::string>("levelcell-background-type", "thumbnail"),
        {"gradient", "thumbnail"},
        [](std::string const& v) { ssaved<std::string>("levelcell-background-type", v); },
        w));

    c->addChild(createSliderRow("Background Blur",
        static_cast<float>(gsaved<double>("levelcell-background-blur", 3.0)),
        0.0f, 10.0f,
        [](float v) { ssaved<double>("levelcell-background-blur", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Background Darkness",
        static_cast<float>(gsaved<double>("levelcell-background-darkness", 0.2)),
        0.0f, 1.0f,
        [](float v) { ssaved<double>("levelcell-background-darkness", static_cast<double>(v)); },
        w));

    c->addChild(createSectionHeader("Estructura", w));

    c->addChild(createToggleRow("Show Separator",
        gsaved<bool>("levelcell-show-separator", true),
        [](bool v) { ssaved<bool>("levelcell-show-separator", v); },
        w));

    c->addChild(createToggleRow("Show View Button",
        gsaved<bool>("levelcell-show-view-button", true),
        [](bool v) { ssaved<bool>("levelcell-show-view-button", v); },
        w));

    c->addChild(createToggleRow("Compact Mode",
        gset<bool>("compact-list-mode"),
        [](bool v) { sset<bool>("compact-list-mode", v); },
        w));

    c->addChild(createToggleRow("Show Compact Toggle",
        gsaved<bool>("compact-list-show-toggle", true),
        [](bool v) { ssaved<bool>("compact-list-show-toggle", v); },
        w));
}

void buildThumbnailGalleryGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Galeria en Celda", w));

    c->addChild(createToggleRow("Auto-Cycle Gallery",
        gsaved<bool>("levelcell-gallery-autocycle", true),
        [](bool v) { ssaved<bool>("levelcell-gallery-autocycle", v); },
        w));

    c->addChild(createDropdownRow("Transition Type",
        gsaved<std::string>("levelcell-gallery-transition", "crossfade"),
        {"crossfade", "slide-left", "slide-right", "slide-up", "slide-down",
         "zoom-in", "zoom-out", "flip-horizontal", "flip-vertical",
         "rotate-cw", "rotate-ccw", "cube", "dissolve", "swipe", "bounce", "random"},
        [](std::string const& v) { ssaved<std::string>("levelcell-gallery-transition", v); },
        w));

    c->addChild(createSliderRow("Transition Duration",
        static_cast<float>(gsaved<double>("levelcell-gallery-transition-duration", 0.6)),
        0.2f, 2.0f,
        [](float v) { ssaved<double>("levelcell-gallery-transition-duration", static_cast<double>(v)); },
        w));
}

void buildThumbnailHoverGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Animacion al Pasar el Mouse", w));

    c->addChild(createToggleRow("Hover Effects",
        gset<bool>("levelcell-hover-effects"),
        [](bool v) { sset<bool>("levelcell-hover-effects", v); },
        w));

    c->addChild(createDropdownRow("Animation Type",
        gsaved<std::string>("levelcell-anim-type", "zoom-slide"),
        {"none", "zoom-slide", "zoom", "slide", "bounce", "rotate",
         "rotate-content", "shake", "pulse", "swing"},
        [](std::string const& v) { ssaved<std::string>("levelcell-anim-type", v); },
        w));

    c->addChild(createSliderRow("Animation Speed",
        static_cast<float>(gsaved<double>("levelcell-anim-speed", 1.0)),
        0.1f, 5.0f,
        [](float v) { ssaved<double>("levelcell-anim-speed", static_cast<double>(v)); },
        w));

    c->addChild(createSectionHeader("Efecto de Color", w));

    c->addChild(createDropdownRow("Color Effect",
        gsaved<std::string>("levelcell-anim-effect", "none"),
        {"none", "brightness", "darken", "sepia", "red", "blue", "gold",
         "fade", "grayscale", "blur", "invert", "glitch", "sharpen",
         "edge-detection", "vignette", "pixelate", "posterize", "chromatic",
         "scanlines", "solarize", "rainbow"},
        [](std::string const& v) { ssaved<std::string>("levelcell-anim-effect", v); },
        w));

    c->addChild(createToggleRow("Effect on Background",
        gsaved<bool>("levelcell-effect-on-gradient", false),
        [](bool v) { ssaved<bool>("levelcell-effect-on-gradient", v); },
        w));
}

void buildCaptureGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Captura de Miniatura", w));

    c->addChild(createToggleRow("Enable Capture Button",
        gset<bool>("enable-thumbnail-taking"),
        [](bool v) { sset<bool>("enable-thumbnail-taking", v); },
        w));

    c->addChild(createLinkRow("Configurar Capture Keybind (Geode)",
        []() { openNativeSettings(); },
        w));
}

void buildLevelInfoGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Fondo del Nivel", w));

    c->addChild(createDropdownRow("Background Style",
        gset<std::string>("levelinfo-background-style"),
        {"normal", "pixel", "blur", "paimonblur", "grayscale", "sepia",
         "vignette", "scanlines", "bloom", "chromatic", "radial-blur",
         "glitch", "posterize", "rain", "matrix", "neon-pulse",
         "wave-distortion", "crt", "shockwave", "vortex", "magnetic", "spotlight",
         "ripple", "plasma-cursor", "freeze", "pixelate-cursor",
         "kaleidoscope", "sonar", "electric-arc", "prism-split",
         "gravity-well", "shatter", "heat-haze", "liquify",
         "ink-spread", "hologram", "time-warp", "underwater", "neon-trail",
         "synthwave", "neon-city", "ocean", "galaxy"},
        [](std::string const& v) { sset<std::string>("levelinfo-background-style", v); },
        w));

    c->addChild(createIntSliderRow("Effect Intensity",
        gsaved<int>("levelinfo-effect-intensity", 4),
        1, 10,
        [](int v) { ssaved<int>("levelinfo-effect-intensity", v); },
        w));

    c->addChild(createIntSliderRow("Background Darkness",
        gsaved<int>("levelinfo-bg-darkness", 27),
        0, 50,
        [](int v) { ssaved<int>("levelinfo-bg-darkness", v); },
        w));
}

void buildDynamicSongGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Cancion Dinamica", w));

    c->addChild(createToggleRow("Play Level Song on Info",
        gset<bool>("dynamic-song"),
        [](bool v) { sset<bool>("dynamic-song", v); },
        w));

    c->addChild(createHintRow(
        "Reproduce la cancion del nivel en una posicion aleatoria al ver su info.",
        w));
}

void buildProfileMusicGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Musica de Perfil", w));

    c->addChild(createToggleRow("Enable Profile Music",
        gset<bool>("profile-music-enabled"),
        [](bool v) { sset<bool>("profile-music-enabled", v); },
        w));

    c->addChild(createToggleRow("Crossfade",
        gsaved<bool>("profile-music-crossfade", true),
        [](bool v) { ssaved<bool>("profile-music-crossfade", v); },
        w));

    c->addChild(createSliderRow("Fade Duration",
        static_cast<float>(gsaved<double>("profile-music-fade-duration", 0.3)),
        0.1f, 3.0f,
        [](float v) { ssaved<double>("profile-music-fade-duration", static_cast<double>(v)); },
        w));

    c->addChild(createButtonRow("Editar mi Fragmento",
        "Abrir",
        []() {
            auto* acc = GJAccountManager::sharedState();
            int accountID = acc ? acc->m_accountID : 0;
            if (accountID > 0) {
                if (auto* popup = ProfileMusicPopup::create(accountID)) popup->show();
            } else {
                PaimonNotify::create("Necesitas iniciar sesion.", NotificationIcon::Warning)->show();
            }
        },
        w));
}

void buildMenuMusicPlayerGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Reproductor de Musica", w));

    c->addChild(createToggleRow("Enable Menu Music Player",
        gset<bool>("menuMusicEnable"),
        [](bool v) { sset<bool>("menuMusicEnable", v); },
        w));

    c->addChild(createToggleRow("Show Playback Progress",
        gset<bool>("menuLoopShowPlaybackProgress"),
        [](bool v) { sset<bool>("menuLoopShowPlaybackProgress", v); },
        w));

    c->addChild(createToggleRow("Menu Music Hotkeys",
        gset<bool>("menuLoopEnableKeyboardShortcuts"),
        [](bool v) { sset<bool>("menuLoopEnableKeyboardShortcuts", v); },
        w));

    c->addChild(createIntSliderRow("Seek Step (ms)",
        static_cast<int>(gset<int64_t>("menuLoopSeekAmountMs")),
        100, 30000,
        [](int v) { sset<int64_t>("menuLoopSeekAmountMs", static_cast<int64_t>(v)); },
        w));
}

void buildMenuLoopGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Bucle del Menu", w));

    c->addChild(createToggleRow("Menu Loop Shuffle",
        gset<bool>("menuLoopConstantShuffle"),
        [](bool v) { sset<bool>("menuLoopConstantShuffle", v); },
        w));

    c->addChild(createToggleRow("Remember Last Menu Loop",
        gset<bool>("menuLoopSaveSongOnGameClose"),
        [](bool v) { sset<bool>("menuLoopSaveSongOnGameClose", v); },
        w));

    c->addChild(createSectionHeader("Al Salir del Nivel", w));

    c->addChild(createToggleRow("Randomize on Level Exit",
        gset<bool>("menuLoopRandomizeOnLevelExit"),
        [](bool v) { sset<bool>("menuLoopRandomizeOnLevelExit", v); },
        w));

    c->addChild(createToggleRow("Restore Position on Level Exit",
        gset<bool>("menuLoopRestoreOnLevelExit"),
        [](bool v) { sset<bool>("menuLoopRestoreOnLevelExit", v); },
        w));

    c->addChild(createSectionHeader("Al Salir del Editor", w));

    c->addChild(createToggleRow("Randomize on Editor Exit",
        gset<bool>("menuLoopRandomizeOnEditorExit"),
        [](bool v) { sset<bool>("menuLoopRandomizeOnEditorExit", v); },
        w));

    c->addChild(createToggleRow("Restore Position on Editor Exit",
        gset<bool>("menuLoopRestoreOnEditorExit"),
        [](bool v) { sset<bool>("menuLoopRestoreOnEditorExit", v); },
        w));
}

void buildMenuLoopNotificationsGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Notificaciones de Reproduccion", w));

    c->addChild(createToggleRow("Now Playing Notifications",
        gset<bool>("menuLoopEnableNotification"),
        [](bool v) { sset<bool>("menuLoopEnableNotification", v); },
        w));

    c->addChild(createSliderRow("Notification Duration (s)",
        static_cast<float>(gset<double>("menuLoopNotificationTime")),
        0.5f, 5.0f,
        [](float v) { sset<double>("menuLoopNotificationTime", static_cast<double>(v)); },
        w));

    c->addChild(createDropdownRow("Notification Prefix",
        gset<std::string>("menuLoopCustomPrefix"),
        {"Now Playing", "Current Song", "Looping", "Song", "Music", "Playing", "[Empty]"},
        [](std::string const& v) { sset<std::string>("menuLoopCustomPrefix", v); },
        w));
}

void buildPopupAnimationGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Animacion de Entrada", w));

    c->addChild(createToggleRow("Dynamic Popups",
        gset<bool>("dynamic-popup-enabled"),
        [](bool v) { sset<bool>("dynamic-popup-enabled", v); },
        w));

    c->addChild(createDropdownRow("Popup Style",
        gsaved<std::string>("dynamic-popup-style", "paimonUI"),
        {"paimonUI", "jelly", "spiral", "drop-bounce", "skew-pop", "elastic",
         "bounce", "slide-up", "slide-down", "slide-left", "slide-right",
         "zoom-fade", "flip", "fold", "pop-rotate", "elastic-drop",
         "glitch-shake", "card-turn", "fly-spin"},
        [](std::string const& v) { ssaved<std::string>("dynamic-popup-style", v); },
        w));

    c->addChild(createSliderRow("Popup Speed",
        static_cast<float>(gsaved<double>("dynamic-popup-speed", 1.0)),
        0.3f, 3.0f,
        [](float v) { ssaved<double>("dynamic-popup-speed", static_cast<double>(v)); },
        w));

    c->addChild(createHintRow(
        "Aplica a todos los popups de Paimbnails al abrirse.", w));
}

void buildPopupExitGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Animacion de Salida", w));

    c->addChild(createToggleRow("Dynamic Popup Exit",
        gset<bool>("dynamic-exit-enabled"),
        [](bool v) { sset<bool>("dynamic-exit-enabled", v); },
        w));

    c->addChild(createSliderRow("Exit Speed",
        static_cast<float>(gsaved<double>("dynamic-exit-speed", 1.0)),
        0.3f, 3.0f,
        [](float v) { ssaved<double>("dynamic-exit-speed", static_cast<double>(v)); },
        w));

    c->addChild(createHintRow(
        "Animacion al cerrar los popups de Paimbnails.", w));
}

void buildPopupBlurGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Desenfoque de Fondo", w));

    c->addChild(createToggleRow("Popup Blur",
        gset<bool>("popup-blur-enabled"),
        [](bool v) { sset<bool>("popup-blur-enabled", v); },
        w));

    c->addChild(createDropdownRow("Blur Style",
        gsaved<std::string>("popup-blur-style", "paimonblur"),
        {"paimonblur", "gaussian"},
        [](std::string const& v) { ssaved<std::string>("popup-blur-style", v); },
        w));

    c->addChild(createSliderRow("Blur Intensity",
        static_cast<float>(gsaved<double>("popup-blur-intensity", 4.0)),
        0.5f, 10.0f,
        [](float v) { ssaved<double>("popup-blur-intensity", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Blur Darkness",
        static_cast<float>(gsaved<double>("popup-blur-darkness", 0.28)),
        0.0f, 1.0f,
        [](float v) { ssaved<double>("popup-blur-darkness", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Blur Fade Duration",
        static_cast<float>(gsaved<double>("popup-blur-fade-duration", 0.18)),
        0.0f, 0.6f,
        [](float v) { ssaved<double>("popup-blur-fade-duration", static_cast<double>(v)); },
        w));
}

void buildPerformanceGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Descargas y Cache", w));

    c->addChild(createIntSliderRow("Download Threads",
        static_cast<int>(gset<int64_t>("thumbnail-concurrent-downloads")),
        1, 40,
        [](int v) { sset<int64_t>("thumbnail-concurrent-downloads", static_cast<int64_t>(v)); },
        w));

    c->addChild(createToggleRow("Disk Cache",
        gset<bool>("enable-disk-cache"),
        [](bool v) { sset<bool>("enable-disk-cache", v); },
        w));

    c->addChild(createToggleRow("Clear Cache on Exit",
        gset<bool>("clear-cache-on-exit"),
        [](bool v) { sset<bool>("clear-cache-on-exit", v); },
        w));

    c->addChild(createSectionHeader("Atajos", w));

    c->addChild(createLinkRow("Open Thumbnails Folder",
        []() {
            // Reutilizamos el setting tipo "button" del propio mod.
            // El handler ya esta registrado en MaintenanceActions.cpp.
            openNativeSettings();
        },
        w));
}

void buildScoreCellGroup(CCNode* c, float w) {
    c->addChild(createSectionHeader("Estilo de Celdas de Puntuacion", w));

    c->addChild(createDropdownRow("Score Cell Style",
        gsaved<std::string>("score-cell-style", "default"),
        {"default", "compact", "expanded", "minimal"},
        [](std::string const& v) { ssaved<std::string>("score-cell-style", v); },
        w));

    c->addChild(createHintRow(
        "Aplica a las celdas de leaderboards y scores en el juego.", w));
}

// ─────────────────────────────────────────────────────────────────────────────
// Registro
// ─────────────────────────────────────────────────────────────────────────────

std::unordered_map<std::string, FeatureGroup> const& featureGroupRegistry() {
    static const std::unordered_map<std::string, FeatureGroup> registry = {
        {"general",
            {"General", "Idioma, updates y atajos.", &buildGeneralGroup}},
        {"thumbnail-layout",
            {"Miniaturas - Celda", "Tamano y fondo de las celdas de niveles.", &buildThumbnailLayoutGroup}},
        {"thumbnail-gallery",
            {"Miniaturas - Galeria", "Auto-cycle y transiciones en celda.", &buildThumbnailGalleryGroup}},
        {"thumbnail-hover",
            {"Miniaturas - Hover", "Efectos al pasar el mouse.", &buildThumbnailHoverGroup}},
        {"capture",
            {"Captura de Miniatura", "Boton de pausa y atajos.", &buildCaptureGroup}},
        {"level-info",
            {"Fondo del Nivel", "Estilo, intensidad y oscuridad.", &buildLevelInfoGroup}},
        {"dynamic-song",
            {"Cancion Dinamica", "Reproduccion en pantalla de info.", &buildDynamicSongGroup}},
        {"profile-music",
            {"Musica de Perfil", "Crossfade y fragmento personal.", &buildProfileMusicGroup}},
        {"menu-music-player",
            {"Reproductor del Menu", "Hotkeys, progreso y skip.", &buildMenuMusicPlayerGroup}},
        {"menu-loop",
            {"Bucle del Menu", "Shuffle, randomizar y restaurar.", &buildMenuLoopGroup}},
        {"menu-loop-notifications",
            {"Notificaciones Now Playing", "Toggle, duracion y prefijo.", &buildMenuLoopNotificationsGroup}},
        {"popup-animation",
            {"Animacion de Popups", "Estilo y velocidad de entrada.", &buildPopupAnimationGroup}},
        {"popup-exit",
            {"Salida de Popups", "Animacion al cerrar.", &buildPopupExitGroup}},
        {"popup-blur",
            {"Blur de Popups", "Estilo, intensidad, oscuridad y fade.", &buildPopupBlurGroup}},
        {"performance",
            {"Rendimiento", "Descargas, cache y limpieza.", &buildPerformanceGroup}},
        {"score-cell",
            {"Score Cell", "Estilo de celdas de puntuacion.", &buildScoreCellGroup}},
    };
    return registry;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mapeo: nombre granular (ingles) → group key del registro O accion directa
//
// Si la accion abre un popup dedicado existente (no un FeatureConfigPopup),
// retornamos un nullptr de group key y devolvemos un std::function con la
// accion concreta.
// ─────────────────────────────────────────────────────────────────────────────

struct GranularRoute {
    std::string groupKey;                    // si no esta vacio, abrir FeatureConfigPopup
    std::function<void()> dedicatedAction;   // si esta seteado, ejecutar en lugar de groupKey
};

GranularRoute routeForGranular(std::string const& englishName) {
    static const std::unordered_map<std::string, std::string> nameToGroup = {
        // Cat 0: General
        {"Language / Idioma",                 "general"},
        {"Auto Update",                       "general"},
        {"Quick Search Key",                  "general"},
        {"Realtime Search Preview",           "general"},
        {"Settings Panel Keybind",            "general"},
        {"Layout Editor Keybind",             "general"},
        {"Debug Logs",                        "general"},

        // Cat 1: Miniaturas
        {"Thumbnail Size",                    "thumbnail-layout"},
        {"Background Style (Cell)",           "thumbnail-layout"},
        {"Background Blur (Cell)",            "thumbnail-layout"},
        {"Darkness (Cell)",                   "thumbnail-layout"},
        {"Show Separator",                    "thumbnail-layout"},
        {"Show View Button",                  "thumbnail-layout"},
        {"Compact Mode",                      "thumbnail-layout"},
        {"Show Compact Toggle",               "thumbnail-layout"},
        {"Auto-Cycle Gallery",                "thumbnail-gallery"},
        {"Transition Type",                   "thumbnail-gallery"},
        {"Transition Duration",               "thumbnail-gallery"},
        {"Hover Effects",                     "thumbnail-hover"},
        {"Animation Type",                    "thumbnail-hover"},
        {"Animation Speed",                   "thumbnail-hover"},
        {"Color Effect",                      "thumbnail-hover"},
        {"Effect on Background",              "thumbnail-hover"},
        {"Enable Capture Button",             "capture"},
        {"Capture Thumbnail Key",             "capture"},

        // Cat 2: Nivel
        {"Background Style (Level)",          "level-info"},
        {"Dynamic Song",                      "dynamic-song"},

        // Cat 3: Audio
        {"Enable Profile Music",              "profile-music"},
        {"Enable Menu Music Player",          "menu-music-player"},
        {"Show Playback Progress",            "menu-music-player"},
        {"Menu Music Hotkeys",                "menu-music-player"},
        {"Seek Step (ms)",                    "menu-music-player"},
        {"Menu Loop Shuffle",                 "menu-loop"},
        {"Remember Last Menu Loop",           "menu-loop"},
        {"Randomize on Level Exit",           "menu-loop"},
        {"Restore Position on Level Exit",    "menu-loop"},
        {"Randomize on Editor Exit",          "menu-loop"},
        {"Restore Position on Editor Exit",   "menu-loop"},
        {"Now Playing Notifications",         "menu-loop-notifications"},
        {"Notification Duration",             "menu-loop-notifications"},
        {"Notification Prefix",               "menu-loop-notifications"},

        // Cat 5: Extras
        {"Score Cell Style",                  "score-cell"},
        {"Dynamic Popups",                    "popup-animation"},
        {"Dynamic Popup Exit",                "popup-exit"},
        {"Popup Blur",                        "popup-blur"},
        {"Download Threads",                  "performance"},
        {"Disk Cache",                        "performance"},
        {"Clear Cache on Exit",               "performance"},
        {"Open Thumbnails Folder",            "performance"},
    };

    // 1. Acciones directas a popups dedicados existentes
    if (englishName == "Progress Bar") {
        return {{}, []() { if (auto* p = ProgressBarConfigPopup::create()) p->show(); }};
    }
    if (englishName == "Enable Pet" ||
        englishName == "Pet Sprite / Pet Type" ||
        englishName == "Pet Scale" ||
        englishName == "Pet Opacity") {
        return {{}, []() { if (auto* p = PetConfigPopup::create()) p->show(); }};
    }
    if (englishName == "Custom Cursor" ||
        englishName == "Cursor Trail" ||
        englishName == "Cursor Scale") {
        return {{}, []() { if (auto* p = CursorConfigPopup::create()) p->show(); }};
    }
    if (englishName == "Custom Slider Thumb") {
        return {{}, []() {
            if (auto* p = paimon::slider::CustomSliderPopup::create()) p->show();
        }};
    }
    if (englishName == "Editor Fondos" || englishName == "Configuración Completa") {
        return {{}, []() {
            // PaiConfigLayer es un layer fullscreen — replicamos la ruta del Hub.
            SettingsPanelManager::get().close();
            auto scene = CCDirector::get()->getRunningScene();
            if (!scene) return;
            if (auto* layer = PaiConfigLayer::create()) {
                scene->addChild(layer, 5000);
            }
        }};
    }
    if (englishName == "Transiciones de Fondos") {
        return {{}, []() { if (auto* p = TransitionConfigPopup::create()) p->show(); }};
    }
    if (englishName == "Enable Discord Rich Presence" ||
        englishName == "Configure Discord RPC") {
        return {{}, []() {
            if (auto* p = paimon::discord::DiscordConfigPopup::create()) p->show();
        }};
    }
    if (englishName == "Refresh Discord Status") {
        return {{}, []() {
            paimon::discord::DiscordPresenceManager::get().refreshSoon();
            PaimonNotify::create("Rich Presence actualizada.", NotificationIcon::Success)->show();
        }};
    }

    // 2. Mapeo a un FeatureConfigPopup
    auto it = nameToGroup.find(englishName);
    if (it != nameToGroup.end()) {
        return {it->second, {}};
    }

    // 3. Sin ruta (caller hace fallback al panel)
    return {};
}

} // namespace

namespace paimon::ui {

bool FeatureConfigPopup::hasFeatureKey(std::string const& featureKey) {
    auto const& reg = featureGroupRegistry();
    return reg.find(featureKey) != reg.end();
}

FeatureConfigPopup* FeatureConfigPopup::create(std::string const& featureKey) {
    auto* ret = new FeatureConfigPopup();
    if (ret && ret->init(featureKey)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool FeatureConfigPopup::init(std::string const& featureKey) {
    auto const& reg = featureGroupRegistry();
    auto it = reg.find(featureKey);
    if (it == reg.end()) {
        // No existe el grupo: no abrir
        return false;
    }
    auto const& group = it->second;

    if (!Popup::init(380.f, 260.f)) return false;

    this->setTitle(group.title.c_str());

    auto winSize = m_mainLayer->getContentSize();

    // Subtitle (debajo del titulo)
    if (!group.subtitle.empty()) {
        auto* subtitleLbl = CCLabelBMFont::create(group.subtitle.c_str(), "chatFont.fnt");
        subtitleLbl->setScale(0.55f);
        subtitleLbl->setColor({180, 190, 210});
        subtitleLbl->setAnchorPoint({0.5f, 1.f});
        subtitleLbl->setPosition({winSize.width / 2.f, winSize.height - 28.f});
        m_mainLayer->addChild(subtitleLbl);
    }

    // Layout: ScrollLayer ocupa el espacio bajo el titulo+subtitulo
    float topOffset = group.subtitle.empty() ? 28.f : 44.f;
    float scrollW = winSize.width - 30.f;
    float scrollH = winSize.height - topOffset - 16.f;
    float scrollX = 15.f;
    float scrollY = 8.f;

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    m_scroll->setPosition({scrollX, scrollY});
    m_scroll->setID("paimon-feature-config-scroll"_spr);
    m_mainLayer->addChild(m_scroll);

    auto* contentLayer = m_scroll->m_contentLayer;

    // El builder agrega cada row como hijo de 'tmp'. Despues movemos cada row
    // a m_contentLayer con la posicion calculada (top-down). Asi seguimos el
    // mismo patron de PaimonMultiSettingsPanel y evitamos enredarnos con
    // anchor points anidados.
    auto* tmp = CCNode::create();
    tmp->setContentSize({scrollW, scrollH});
    group.build(tmp, scrollW);

    std::vector<CCNode*> rows;
    if (auto* children = tmp->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            rows.push_back(child);
        }
    }

    float totalH = 0.f;
    for (auto* r : rows) totalH += r->getContentSize().height;

    float padBottom = 8.f;
    float contentH = std::max(scrollH, totalH + padBottom);
    contentLayer->setContentSize({scrollW, contentH});

    float currentY = contentH;
    for (auto* r : rows) {
        float h = r->getContentSize().height;
        currentY -= h;
        r->retain();
        r->removeFromParent();
        r->setAnchorPoint({0.f, 0.f});
        r->setPosition({0.f, currentY});
        contentLayer->addChild(r);
        r->release();
    }

    m_scroll->moveToTop();
    return true;
}

void openFeatureConfigFor(std::string const& englishGranularName,
                          int fallbackCategoryIndex) {
    auto route = routeForGranular(englishGranularName);

    // 1. Accion dedicada (popup existente, layer fullscreen, etc.)
    if (route.dedicatedAction) {
        route.dedicatedAction();
        return;
    }

    // 2. FeatureConfigPopup con un groupKey registrado
    if (!route.groupKey.empty() && FeatureConfigPopup::hasFeatureKey(route.groupKey)) {
        if (auto* popup = FeatureConfigPopup::create(route.groupKey)) {
            popup->show();
            return;
        }
    }

    // 3. Fallback: panel de settings tradicional con la categoria correspondiente.
    SettingsPanelManager::get().open(fallbackCategoryIndex);
}

} // namespace paimon::ui
