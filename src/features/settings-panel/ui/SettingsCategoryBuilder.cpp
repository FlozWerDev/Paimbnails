#include "SettingsCategoryBuilder.hpp"
#include "SettingsControls.hpp"
#include "../services/SettingsPanelManager.hpp"
#include "../../../core/Settings.hpp"
#include "../../../layers/PaiConfigLayer.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../features/pet/services/PetManager.hpp"
#include "../../../features/pet/ui/PetConfigPopup.hpp"
#include "../../../features/transitions/services/TransitionManager.hpp"
#include "../../../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../../../features/cursor/services/CursorManager.hpp"
#include "../../../features/cursor/ui/CursorConfigPopup.hpp"
#include "../../../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../../../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../../../features/profile-music/services/ProfileMusicManager.hpp"
#include "../../../features/discord-presence/ui/DiscordConfigPopup.hpp"
#include "../../../features/discord-presence/services/DiscordPresenceManager.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace cocos2d;
using namespace geode::prelude;

// Setting read/write helpers

namespace {

// getSettingValue with saved-value fallback
template<typename T>
T gset(const char* key) {
    if (Mod::get()->hasSetting(key)) return Mod::get()->getSettingValue<T>(key);
    return Mod::get()->getSavedValue<T>(key, T{});
}

// setSettingValue with saved-value fallback
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

using namespace paimon::settings_ui;

void openNativeModSettingsPopup() {
    SettingsPanelManager::get().close();
    paimon::scheduleMainThreadDelay(0.18f, []() {
        geode::openSettingsPopup(Mod::get(), false);
    });
}

// CATEGORIA 0: General

void buildGeneral(CCNode* c, float w) {
    c->addChild(createSectionHeader("General", w));

    c->addChild(createDropdownRow("Language",
        gset<std::string>("language"),
        {"english", "spanish", "portuguese", "french", "german", "russian", "japanese"},
        [](std::string const& v){ sset<std::string>("language", v); },
        w));

    c->addChild(createToggleRow("Debug Logs",
        gset<bool>("enable-debug-logs"),
        [](bool v){ sset<bool>("enable-debug-logs", v); },
        w));

    c->addChild(createSectionHeader("Settings Panel", w));

    c->addChild(createLinkRow("Configure Panel Keybind (Geode)",
        [](){
            openNativeModSettingsPopup();
        },
        w));
}

// CATEGORIA 1: Level Thumbnails

void buildLevelThumbnails(CCNode* c, float w) {
    c->addChild(createSectionHeader("Thumbnail Layout", w));

    c->addChild(createSliderRow("Thumb Width",
        static_cast<float>(gset<double>("level-thumb-width")),
        0.2f, 0.95f,
        [](float v){ sset<double>("level-thumb-width", static_cast<double>(v)); },
        w));

    c->addChild(createDropdownRow("Background Style",
        gsaved<std::string>("levelcell-background-type", "thumbnail"),
        {"gradient", "thumbnail"},
        [](std::string const& v){ ssaved<std::string>("levelcell-background-type", v); },
        w));

    c->addChild(createSliderRow("Background Blur",
        static_cast<float>(gsaved<double>("levelcell-background-blur", 3.0)),
        0.0f, 10.0f,
        [](float v){ ssaved<double>("levelcell-background-blur", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Darkness",
        static_cast<float>(gsaved<double>("levelcell-background-darkness", 0.2)),
        0.0f, 1.0f,
        [](float v){ ssaved<double>("levelcell-background-darkness", static_cast<double>(v)); },
        w));

    c->addChild(createToggleRow("Show Separator",
        gsaved<bool>("levelcell-show-separator", true),
        [](bool v){ ssaved<bool>("levelcell-show-separator", v); },
        w));

    c->addChild(createToggleRow("Show View Button",
        gsaved<bool>("levelcell-show-view-button", true),
        [](bool v){ ssaved<bool>("levelcell-show-view-button", v); },
        w));

    c->addChild(createToggleRow("Compact List Mode",
        gset<bool>("compact-list-mode"),
        [](bool v){ sset<bool>("compact-list-mode", v); },
        w));

    c->addChild(createToggleRow("Show Compact Toggle",
        gsaved<bool>("compact-list-show-toggle", true),
        [](bool v){ ssaved<bool>("compact-list-show-toggle", v); },
        w));

    c->addChild(createSectionHeader("Gallery", w));

    c->addChild(createToggleRow("Auto-Cycle",
        gsaved<bool>("levelcell-gallery-autocycle", true),
        [](bool v){ ssaved<bool>("levelcell-gallery-autocycle", v); },
        w));

    c->addChild(createDropdownRow("Transition",
        gsaved<std::string>("levelcell-gallery-transition", "crossfade"),
        {"crossfade","slide-left","slide-right","slide-up","slide-down",
         "zoom-in","zoom-out","flip-horizontal","flip-vertical",
         "rotate-cw","rotate-ccw","cube","dissolve","swipe","bounce","random"},
        [](std::string const& v){ ssaved<std::string>("levelcell-gallery-transition", v); },
        w));

    c->addChild(createSliderRow("Transition Duration",
        static_cast<float>(gsaved<double>("levelcell-gallery-transition-duration", 0.6)),
        0.2f, 2.0f,
        [](float v){ ssaved<double>("levelcell-gallery-transition-duration", static_cast<double>(v)); },
        w));
}

// CATEGORIA 2: Visual Effects (LevelCell)

void buildVisualEffects(CCNode* c, float w) {
    c->addChild(createSectionHeader("Hover Animation", w));

    c->addChild(createToggleRow("Hover Effects",
        gset<bool>("levelcell-hover-effects"),
        [](bool v){ sset<bool>("levelcell-hover-effects", v); },
        w));

    c->addChild(createDropdownRow("Animation Type",
        gsaved<std::string>("levelcell-anim-type", "zoom-slide"),
        {"none","zoom-slide","zoom","slide","bounce","rotate","rotate-content","shake","pulse","swing"},
        [](std::string const& v){ ssaved<std::string>("levelcell-anim-type", v); },
        w));

    c->addChild(createSliderRow("Animation Speed",
        static_cast<float>(gsaved<double>("levelcell-anim-speed", 1.0)),
        0.1f, 5.0f,
        [](float v){ ssaved<double>("levelcell-anim-speed", static_cast<double>(v)); },
        w));

    c->addChild(createDropdownRow("Color Effect",
        gsaved<std::string>("levelcell-anim-effect", "none"),
        {"none","brightness","darken","sepia","red","blue","gold","fade","grayscale","blur",
         "invert","glitch","sharpen","edge-detection","vignette","pixelate","posterize",
         "chromatic","scanlines","solarize","rainbow"},
        [](std::string const& v){ ssaved<std::string>("levelcell-anim-effect", v); },
        w));

    c->addChild(createToggleRow("Effect on Background",
        gsaved<bool>("levelcell-effect-on-gradient", false),
        [](bool v){ ssaved<bool>("levelcell-effect-on-gradient", v); },
        w));

    c->addChild(createSectionHeader("Cell Extras", w));

    c->addChild(createToggleRow("Mythic Particles",
        gsaved<bool>("levelcell-mythic-particles", true),
        [](bool v){ ssaved<bool>("levelcell-mythic-particles", v); },
        w));

    c->addChild(createToggleRow("Animated Gradient",
        gsaved<bool>("levelcell-animated-gradient", true),
        [](bool v){ ssaved<bool>("levelcell-animated-gradient", v); },
        w));
}

// CATEGORIA 3: Level Info Screen

void buildLevelInfo(CCNode* c, float w) {
    c->addChild(createSectionHeader("Level Info Background", w));

    c->addChild(createDropdownRow("Background Style",
        gset<std::string>("levelinfo-background-style"),
        {"normal","pixel","blur","paimonblur","grayscale","sepia","vignette","scanlines","bloom",
         "chromatic","radial-blur","glitch","posterize","rain","matrix","neon-pulse",
         "wave-distortion","crt","shockwave","vortex","magnetic","spotlight",
         "ripple","plasma-cursor","freeze","pixelate-cursor",
         "kaleidoscope","sonar","electric-arc","prism-split",
         "gravity-well","shatter","heat-haze","liquify",
         "ink-spread","hologram","time-warp","underwater","neon-trail",
         "synthwave","neon-city","ocean","galaxy"},
        [](std::string const& v){ sset<std::string>("levelinfo-background-style", v); },
        w));

    c->addChild(createIntSliderRow("Effect Intensity",
        gsaved<int>("levelinfo-effect-intensity", 4),
        1, 10,
        [](int v){ ssaved<int>("levelinfo-effect-intensity", v); },
        w));

    c->addChild(createIntSliderRow("Background Darkness",
        gsaved<int>("levelinfo-bg-darkness", 27),
        0, 50,
        [](int v){ ssaved<int>("levelinfo-bg-darkness", v); },
        w));

    c->addChild(createSectionHeader("Dynamic Song", w));

    c->addChild(createToggleRow("Play Level Song on Info",
        gset<bool>("dynamic-song"),
        [](bool v){ sset<bool>("dynamic-song", v); },
        w));
}

// CATEGORIA 4: Profile Music

void buildProfileMusic(CCNode* c, float w) {
    c->addChild(createSectionHeader("Profile Music", w));

    c->addChild(createToggleRow("Enable",
        gset<bool>("profile-music-enabled"),
        [](bool v){ sset<bool>("profile-music-enabled", v); },
        w));

    c->addChild(createToggleRow("Crossfade",
        gsaved<bool>("profile-music-crossfade", true),
        [](bool v){ ssaved<bool>("profile-music-crossfade", v); },
        w));

    c->addChild(createSliderRow("Fade Duration",
        static_cast<float>(gsaved<double>("profile-music-fade-duration", 0.3)),
        0.1f, 3.0f,
        [](float v){ ssaved<double>("profile-music-fade-duration", static_cast<double>(v)); },
        w));
}

// CATEGORIA 5: Capture

void buildCapture(CCNode* c, float w) {
    c->addChild(createSectionHeader("Thumbnail Capture", w));

    c->addChild(createToggleRow("Enable Capture Button",
        gset<bool>("enable-thumbnail-taking"),
        [](bool v){ sset<bool>("enable-thumbnail-taking", v); },
        w));

    c->addChild(createDropdownRow("Capture Resolution",
        gset<std::string>("capture-resolution"),
        {"1080p", "1440p", "4k"},
        [](std::string const& v){ sset<std::string>("capture-resolution", v); },
        w));

    c->addChild(createLinkRow("Configure Capture Keybind (Geode)",
        [](){
            openNativeModSettingsPopup();
        },
        w));
}

// CATEGORIA 6: Performance

void buildPerformance(CCNode* c, float w) {
    c->addChild(createSectionHeader("Cache & Downloads", w));

    c->addChild(createToggleRow("GIF RAM Cache",
        gsaved<bool>("gif-ram-cache", true),
        [](bool v){ ssaved<bool>("gif-ram-cache", v); },
        w));

    c->addChild(createIntSliderRow("Concurrent Downloads",
        static_cast<int>(gset<int64_t>("thumbnail-concurrent-downloads")),
        1, 20,
        [](int v){ sset<int64_t>("thumbnail-concurrent-downloads", static_cast<int64_t>(v)); },
        w));
}

// CATEGORIA 7: Interface / Popups

void buildInterface(CCNode* c, float w) {
    c->addChild(createSectionHeader("Profile Image", w));

    c->addChild(createIntSliderRow("Profile Image Z-Layer",
        gsaved<int>("profile-img-zlayer", -1),
        -10, 10,
        [](int v){ ssaved<int>("profile-img-zlayer", v); },
        w));

    c->addChild(createSectionHeader("Popup Animations", w));

    c->addChild(createToggleRow("Dynamic Popups",
        gset<bool>("dynamic-popup-enabled"),
        [](bool v){ sset<bool>("dynamic-popup-enabled", v); },
        w));

    c->addChild(createDropdownRow("Popup Style",
        gsaved<std::string>("dynamic-popup-style", "paimonUI"),
        {"paimonUI", "jelly", "spiral", "drop-bounce", "skew-pop", "elastic", "bounce", "slide-up", "slide-down", "slide-left", "slide-right", "zoom-fade", "flip", "fold", "pop-rotate", "elastic-drop", "glitch-shake", "card-turn", "fly-spin"},
        [](std::string const& v){ ssaved<std::string>("dynamic-popup-style", v); },
        w));

    c->addChild(createSliderRow("Popup Speed",
        static_cast<float>(gsaved<double>("dynamic-popup-speed", 1.0)),
        0.3f, 3.0f,
        [](float v){ ssaved<double>("dynamic-popup-speed", static_cast<double>(v)); },
        w));

    c->addChild(createToggleRow("Dynamic Popup Exit",
        gset<bool>("dynamic-exit-enabled"),
        [](bool v){ sset<bool>("dynamic-exit-enabled", v); },
        w));

    c->addChild(createSliderRow("Exit Speed",
        static_cast<float>(gsaved<double>("dynamic-exit-speed", 1.0)),
        0.3f, 3.0f,
        [](float v){ ssaved<double>("dynamic-exit-speed", static_cast<double>(v)); },
        w));

    c->addChild(createSectionHeader("Popup Blur", w));

    c->addChild(createToggleRow("Enable Popup Blur",
        gset<bool>("popup-blur-enabled"),
        [](bool v){ sset<bool>("popup-blur-enabled", v); },
        w));

    c->addChild(createDropdownRow("Blur Style",
        gsaved<std::string>("popup-blur-style", "paiblur"),
        {"paiblur", "paimonblur", "gaussian"},
        [](std::string const& v){ ssaved<std::string>("popup-blur-style", v); },
        w));

    c->addChild(createSliderRow("Blur Intensity",
        static_cast<float>(gsaved<double>("popup-blur-intensity", 4.0)),
        0.5f, 10.0f,
        [](float v){ ssaved<double>("popup-blur-intensity", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Blur Darkness",
        static_cast<float>(gsaved<double>("popup-blur-darkness", 0.28)),
        0.0f, 1.0f,
        [](float v){ ssaved<double>("popup-blur-darkness", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Blur Fade Duration",
        static_cast<float>(gsaved<double>("popup-blur-fade-duration", 0.18)),
        0.0f, 0.6f,
        [](float v){ ssaved<double>("popup-blur-fade-duration", static_cast<double>(v)); },
        w));
}

// CATEGORIA 8: Custom Backgrounds

void buildBackgrounds(CCNode* c, float w) {
    c->addChild(createSectionHeader("Per-Layer Backgrounds", w));

    static const std::vector<std::pair<std::string,std::string>> LAYERS = {
        {"menu",        "Menu"},
        {"levelinfo",   "Level Info"},
        {"levelselect", "Level Select"},
        {"creator",     "Creator"},
        {"browser",     "Browser"},
        {"search",      "Search"},
        {"leaderboards","Leaderboards"},
        {"profile",     "Profile"},
    };

    static const std::vector<std::string> BG_TYPES = {
        "default", "custom", "random", "menu", "id"
    };
    static const std::vector<std::string> SHADERS = {
        "none","grayscale","sepia","vignette","bloom","chromatic","pixelate","posterize","scanlines"
    };

    for (auto const& [key, displayName] : LAYERS) {
        auto hdr = CCLabelBMFont::create(displayName.c_str(), "goldFont.fnt");
        hdr->setScale(0.30f);
        hdr->setAnchorPoint({0.f, 0.5f});
        auto hdrRow = CCNode::create();
        hdrRow->setContentSize({w, HEADER_HEIGHT - 4.f});
        hdrRow->addChild(hdr);
        hdr->setPosition({LABEL_X + 4.f, (HEADER_HEIGHT - 4.f) / 2.f});
        c->addChild(hdrRow);

        auto cfg = LayerBackgroundManager::get().getConfig(key);
        c->addChild(createDropdownRow((displayName + " Type").c_str(),
            cfg.type,
            BG_TYPES,
            [k = key](std::string const& v){
                auto cur = LayerBackgroundManager::get().getConfig(k);
                cur.type = v;
                LayerBackgroundManager::get().saveConfig(k, cur);
            },
            w));

        c->addChild(createToggleRow((displayName + " Dark Mode").c_str(),
            cfg.darkMode,
            [k = key](bool v){
                auto cur = LayerBackgroundManager::get().getConfig(k);
                cur.darkMode = v;
                LayerBackgroundManager::get().saveConfig(k, cur);
            },
            w));


        c->addChild(createSliderRow((displayName + " Darkness").c_str(),
            cfg.darkIntensity,
            0.0f, 1.0f,
            [k = key](float v){
                auto cur = LayerBackgroundManager::get().getConfig(k);
                cur.darkIntensity = v;
                LayerBackgroundManager::get().saveConfig(k, cur);
            },
            w));

        c->addChild(createDropdownRow((displayName + " Shader").c_str(),
            cfg.shader,
            SHADERS,
            [k = key](std::string const& v){
                auto cur = LayerBackgroundManager::get().getConfig(k);
                cur.shader = v;
                LayerBackgroundManager::get().saveConfig(k, cur);
            },
            w));
    }


    c->addChild(createLinkRow("Full Background Editor",
        [](){
            // close panel before opening PaiConfigLayer (fullscreen)
            SettingsPanelManager::get().close();
            auto scene = CCDirector::get()->getRunningScene();
            if (!scene) return;
            auto layer = PaiConfigLayer::create();
            if (layer) scene->addChild(layer, 5000);
        },
        w));
}

// CATEGORIA 9: Custom Transitions

void buildTransitions(CCNode* c, float w) {
    c->addChild(createSectionHeader("Scene Transitions", w));

    c->addChild(createToggleRow("Enable Custom Transitions",
        TransitionManager::get().isEnabled(),
        [](bool v){
            TransitionManager::get().setEnabled(v);
            TransitionManager::get().saveConfig();
        },
        w));

    c->addChild(createLinkRow("Open Transition Editor",
        [](){
            auto popup = TransitionConfigPopup::create();
            if (popup) popup->show();
        },
        w));
}

// CATEGORIA 10: Pet System

void buildPet(CCNode* c, float w) {
    auto& cfg = PetManager::get().config();

    auto save = [](){
        PetManager::get().applyConfigLive();
    };

    c->addChild(createSectionHeader("Pet", w));

    c->addChild(createToggleRow("Enable Pet",
        cfg.enabled,
        [save](bool v){ PetManager::get().config().enabled = v; save(); },
        w));

    c->addChild(createSliderRow("Scale",
        cfg.scale, 0.1f, 3.0f,
        [save](float v){ PetManager::get().config().scale = v; save(); },
        w));

    c->addChild(createSliderRow("Cursor Sensitivity",
        cfg.sensitivity, 0.01f, 1.0f,
        [save](float v){ PetManager::get().config().sensitivity = v; save(); },
        w));

    c->addChild(createIntSliderRow("Opacity",
        cfg.opacity, 0, 255,
        [save](int v){ PetManager::get().config().opacity = v; save(); },
        w));

    c->addChild(createSectionHeader("Movement", w));

    c->addChild(createToggleRow("Bounce",
        cfg.bounce,
        [save](bool v){ PetManager::get().config().bounce = v; save(); },
        w));

    c->addChild(createSliderRow("Bounce Height",
        cfg.bounceHeight, 0.f, 20.f,
        [save](float v){ PetManager::get().config().bounceHeight = v; save(); },
        w));

    c->addChild(createSliderRow("Bounce Speed",
        cfg.bounceSpeed, 0.5f, 10.f,
        [save](float v){ PetManager::get().config().bounceSpeed = v; save(); },
        w));

    c->addChild(createSliderRow("Rotation Damping",
        cfg.rotationDamping, 0.f, 1.f,
        [save](float v){ PetManager::get().config().rotationDamping = v; save(); },
        w));

    c->addChild(createSliderRow("Max Tilt",
        cfg.maxTilt, 0.f, 45.f,
        [save](float v){ PetManager::get().config().maxTilt = v; save(); },
        w));

    c->addChild(createToggleRow("Flip on Direction",
        cfg.flipOnDirection,
        [save](bool v){ PetManager::get().config().flipOnDirection = v; save(); },
        w));

    c->addChild(createSectionHeader("Trail", w));

    c->addChild(createToggleRow("Show Trail",
        cfg.showTrail,
        [save](bool v){ PetManager::get().config().showTrail = v; save(); },
        w));

    c->addChild(createSliderRow("Trail Length",
        cfg.trailLength, 5.f, 100.f,
        [save](float v){ PetManager::get().config().trailLength = v; save(); },
        w));

    c->addChild(createSliderRow("Trail Width",
        cfg.trailWidth, 1.f, 20.f,
        [save](float v){ PetManager::get().config().trailWidth = v; save(); },
        w));

    c->addChild(createSectionHeader("Idle & Squish", w));

    c->addChild(createToggleRow("Idle Animation",
        cfg.idleAnimation,
        [save](bool v){ PetManager::get().config().idleAnimation = v; save(); },
        w));

    c->addChild(createSliderRow("Breath Scale",
        cfg.idleBreathScale, 0.f, 0.15f,
        [save](float v){ PetManager::get().config().idleBreathScale = v; save(); },
        w));

    c->addChild(createSliderRow("Breath Speed",
        cfg.idleBreathSpeed, 0.5f, 5.f,
        [save](float v){ PetManager::get().config().idleBreathSpeed = v; save(); },
        w));

    c->addChild(createToggleRow("Squish on Land",
        cfg.squishOnLand,
        [save](bool v){ PetManager::get().config().squishOnLand = v; save(); },
        w));

    c->addChild(createSliderRow("Squish Amount",
        cfg.squishAmount, 0.f, 0.5f,
        [save](float v){ PetManager::get().config().squishAmount = v; save(); },
        w));

    c->addChild(createSectionHeader("Position Offset", w));

    c->addChild(createSliderRow("Offset X",
        cfg.offsetX, -50.f, 50.f,
        [save](float v){ PetManager::get().config().offsetX = v; save(); },
        w));

    c->addChild(createSliderRow("Offset Y",
        cfg.offsetY, -50.f, 100.f,
        [save](float v){ PetManager::get().config().offsetY = v; save(); },
        w));

    // link al editor completo
    c->addChild(createLinkRow("Open Full Pet Config",
        [](){
            auto popup = PetConfigPopup::create();
            if (popup) popup->show();
        },
        w));
}

// CATEGORIA: Custom Cursor

void buildCursor(CCNode* c, float w) {
    auto& cfg = CursorManager::get().config();
    auto save = [](){ CursorManager::get().saveConfig(); CursorManager::get().applyConfigLive(); };

    c->addChild(createSectionHeader("Custom Cursor", w));

    c->addChild(createToggleRow("Enable Cursor",
        cfg.enabled,
        [save](bool v){ CursorManager::get().config().enabled = v; save(); },
        w));

    c->addChild(createSliderRow("Scale",
        cfg.scale, 0.2f, 3.0f,
        [save](float v){ CursorManager::get().config().scale = v; save(); },
        w));

    c->addChild(createIntSliderRow("Opacity",
        cfg.opacity, 0, 255,
        [save](int v){ CursorManager::get().config().opacity = v; save(); },
        w));

    c->addChild(createToggleRow("Trail",
        cfg.trailEnabled,
        [save](bool v){ CursorManager::get().config().trailEnabled = v; save(); },
        w));

    c->addChild(createLinkRow("Open Full Cursor Config",
        [](){
            auto popup = CursorConfigPopup::create();
            if (popup) popup->show();
        },
        w));
}

// CATEGORIA 11: Score Cells / Leaderboard

void buildScoreCells(CCNode* c, float w) {
    c->addChild(createSectionHeader("Leaderboard Cells", w));

    c->addChild(createDropdownRow("Background Type",
        gsaved<std::string>("scorecell-background-type", "thumbnail"),
        {"thumbnail", "gradient"},
        [](std::string const& v){ ssaved<std::string>("scorecell-background-type", v); },
        w));

    c->addChild(createSliderRow("Blur",
        gsaved<float>("scorecell-background-blur", 3.0f),
        0.0f, 10.0f,
        [](float v){ ssaved<float>("scorecell-background-blur", v); },
        w));

    c->addChild(createSliderRow("Darkness",
        gsaved<float>("scorecell-background-darkness", 0.2f),
        0.0f, 1.0f,
        [](float v){ ssaved<float>("scorecell-background-darkness", v); },
        w));

    c->addChild(createSectionHeader("Profile Thumbnail", w));

    c->addChild(createSliderRow("Profile Thumb Width",
        gsaved<float>("profile-thumb-width", 0.6f),
        0.2f, 0.95f,
        [](float v){ ssaved<float>("profile-thumb-width", v); },
        w));
}

void buildGlobalMusic(CCNode* c, float w) {
    c->addChild(createSectionHeader("Global Layer Music", w));

    // seleccion del layer activo
    static const std::vector<std::string> LAYERS_KEYS = {
        "menu","levelinfo","levelselect","creator","browser","search","leaderboards","profile"
    };
    static const std::vector<std::string> MUSIC_MODES = {
        "default","newgrounds","custom","dynamic"
    };
    static const std::vector<std::string> AUDIO_FILTERS = {
        "none","cave","underwater","echo","hall","radio","phone",
        "chorus","flanger","distortion","tremolo","nightcore","vaporwave"
    };

    for (auto const& key : LAYERS_KEYS) {
        auto modeKey    = "layermusic-" + key + "-mode";
        auto filterKey  = "layermusic-" + key + "-filter";
        auto speedKey   = "layermusic-" + key + "-speed";
        auto randomKey  = "layermusic-" + key + "-randomstart";

        std::string displayKey = key;
        displayKey[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(displayKey[0])));

        c->addChild(createDropdownRow((displayKey + " Mode").c_str(),
            gsaved<std::string>(modeKey.c_str(), "default"),
            MUSIC_MODES,
            [mk = modeKey](std::string const& v){ ssaved<std::string>(mk.c_str(), v); },
            w));

        c->addChild(createDropdownRow((displayKey + " Filter").c_str(),
            gsaved<std::string>(filterKey.c_str(), "none"),
            AUDIO_FILTERS,
            [fk = filterKey](std::string const& v){ ssaved<std::string>(fk.c_str(), v); },
            w));

        c->addChild(createSliderRow((displayKey + " Speed").c_str(),
            gsaved<float>(speedKey.c_str(), 1.0f),
            0.1f, 2.0f,
            [sk = speedKey](float v){ ssaved<float>(sk.c_str(), v); },
            w));

        c->addChild(createToggleRow((displayKey + " Random Start").c_str(),
            gsaved<bool>(randomKey.c_str(), false),
            [rk = randomKey](bool v){ ssaved<bool>(rk.c_str(), v); },
            w));
    }
}

// CATEGORIA 13: Maintenance

void buildMaintenance(CCNode* c, float w) {
    c->addChild(createSectionHeader("Cache", w));

    c->addChild(createToggleRow("Clear Cache on Exit",
        gset<bool>("clear-cache-on-exit"),
        [](bool v){ sset<bool>("clear-cache-on-exit", v); },
        w));

    c->addChild(createSectionHeader("Actions", w));

    // Use native Geode settings for button-type controls
    c->addChild(createButtonRow("Run Cleanup", "Run",
        [](){
            // clear thumbnails, GIFs, and profile music caches
            ThumbnailLoader::get().cleanup();
            ThumbnailLoader::get().clearPendingQueue();
            ThumbnailLoader::get().clearCache();
            ProfileMusicManager::get().clearCache();
            PaimonNotify::create("Cleanup complete.", NotificationIcon::Success)->show();
        },
        w));

    c->addChild(createButtonRow("Fetch Mod Code", "Fetch",
        [](){
            PaimonNotify::create("Use Geode mod settings to fetch your mod code.", NotificationIcon::Info)->show();
        },
        w));

    c->addChild(createLinkRow("Geode Settings",
        [](){
            openNativeModSettingsPopup();
        },
        w));
}

// CATEGORIA: Discord Rich Presence

void buildDiscord(CCNode* c, float w) {
    c->addChild(createSectionHeader("Rich Presence", w));

    c->addChild(createToggleRow("Enable Rich Presence",
        gset<bool>("discord-rpc-enabled"),
        [](bool v){
            sset<bool>("discord-rpc-enabled", v);
            paimon::discord::DiscordPresenceManager::get().refreshSoon();
        },
        w));

    c->addChild(createSectionHeader("Configuration", w));

    c->addChild(createLinkRow("Open Discord Config",
        [](){
            if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show();
        },
        w));
}

// CATEGORIA: Menu Physics

void buildMenuPhysics(CCNode* c, float w) {
    c->addChild(createSectionHeader("Menu Physics", w));

    c->addChild(createToggleRow("Enable Menu Physics",
        gset<bool>("menu-physics-enable"),
        [](bool v){ sset<bool>("menu-physics-enable", v); },
        w));

    c->addChild(createSectionHeader("Tuning", w));

    c->addChild(createSliderRow("Gravity",
        static_cast<float>(gset<double>("menu-physics-gravity")),
        -100.f, 100.f,
        [](float v){ sset<double>("menu-physics-gravity", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Bounciness",
        static_cast<float>(gset<double>("menu-physics-bounciness")),
        0.f, 1.f,
        [](float v){ sset<double>("menu-physics-bounciness", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Friction",
        static_cast<float>(gset<double>("menu-physics-friction")),
        0.f, 1.f,
        [](float v){ sset<double>("menu-physics-friction", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Air Drag",
        static_cast<float>(gset<double>("menu-physics-air-drag")),
        0.f, 1.f,
        [](float v){ sset<double>("menu-physics-air-drag", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Spin Drag",
        static_cast<float>(gset<double>("menu-physics-angular-drag")),
        0.f, 2.f,
        [](float v){ sset<double>("menu-physics-angular-drag", static_cast<double>(v)); },
        w));

    c->addChild(createSliderRow("Push Power",
        static_cast<float>(gset<double>("menu-physics-push-power")),
        0.f, 2.f,
        [](float v){ sset<double>("menu-physics-push-power", static_cast<double>(v)); },
        w));

    c->addChild(createToggleRow("Mass By Size",
        gset<bool>("menu-physics-mass-by-size"),
        [](bool v){ sset<bool>("menu-physics-mass-by-size", v); },
        w));

    c->addChild(createToggleRow("Remove Ceiling",
        gset<bool>("menu-physics-remove-ceiling"),
        [](bool v){ sset<bool>("menu-physics-remove-ceiling", v); },
        w));
}

// CATEGORIA: Profile Redesign

void buildProfileRedesign(CCNode* c, float w) {
    c->addChild(createSectionHeader("Profile Redesign", w));

    c->addChild(createToggleRow("Redesign Profile",
        gset<bool>("profile-redesign-enabled"),
        [](bool v){ sset<bool>("profile-redesign-enabled", v); },
        w));
}

// CATEGORIA: Auto Previews

void buildAutoPreview(CCNode* c, float w) {
    c->addChild(createSectionHeader("Auto Previews", w));

    c->addChild(createToggleRow("Auto Previews",
        gset<bool>("auto-preview-enable"),
        [](bool v){ sset<bool>("auto-preview-enable", v); },
        w));

    c->addChild(createDropdownRow("Preview Quality",
        gset<std::string>("auto-preview-quality"),
        {"tiny", "small", "medium"},
        [](std::string const& v){ sset<std::string>("auto-preview-quality", v); },
        w));

    c->addChild(createToggleRow("Show 'AUTO' Badge",
        gset<bool>("auto-preview-badge"),
        [](bool v){ sset<bool>("auto-preview-badge", v); },
        w));

    c->addChild(createSectionHeader("Browser Generation", w));

    c->addChild(createToggleRow("Generate In Level List",
        gset<bool>("auto-preview-browser-gen"),
        [](bool v){ sset<bool>("auto-preview-browser-gen", v); },
        w));

    c->addChild(createIntSliderRow("Max Level Size",
        static_cast<int>(gset<int64_t>("auto-preview-max-objects")),
        5000, 200000,
        [](int v){ sset<int64_t>("auto-preview-max-objects", static_cast<int64_t>(v)); },
        w));

    c->addChild(createIntSliderRow("Max Previews Per Session",
        static_cast<int>(gset<int64_t>("auto-preview-max-per-session")),
        1, 200,
        [](int v){ sset<int64_t>("auto-preview-max-per-session", static_cast<int64_t>(v)); },
        w));

    c->addChild(createLinkRow("Clear Generated Previews (Geode)",
        [](){ openNativeModSettingsPopup(); },
        w));
}

// CATEGORIA: Texture Studio

void buildTextureStudio(CCNode* c, float w) {
    c->addChild(createSectionHeader("Texture Studio", w));

    c->addChild(createToggleRow("Show Texture Studio Button",
        gset<bool>("texture-studio-enabled"),
        [](bool v){ sset<bool>("texture-studio-enabled", v); },
        w));

    c->addChild(createToggleRow("Auto-apply on Generate",
        gset<bool>("texture-studio-auto-apply"),
        [](bool v){ sset<bool>("texture-studio-auto-apply", v); },
        w));

    c->addChild(createToggleRow("Include Medium Quality Port",
        gset<bool>("texture-studio-medium-port"),
        [](bool v){ sset<bool>("texture-studio-medium-port", v); },
        w));
}

// CATEGORIA: Song Search

void buildSongSearch(CCNode* c, float w) {
    c->addChild(createSectionHeader("Song Search", w));

    c->addChild(createToggleRow("Search Song By Name",
        gset<bool>("song-search-enable"),
        [](bool v){ sset<bool>("song-search-enable", v); },
        w));
}

} // anonymous namespace

// Groups with collapsible subcategories

namespace paimon::settings_ui {

std::vector<SettingsGroup> const& getAllGroups() {
    static std::vector<SettingsGroup> s_groups = {
        { "general", "General", {
            { "general",     "General",     buildGeneral     },
            { "maintenance", "Maintenance", buildMaintenance },
        }},
        { "thumbnails", "Thumbnails", {
            { "thumbnails", "Layout & Gallery",  buildLevelThumbnails },
            { "vfx",        "Visual Effects",    buildVisualEffects   },
            { "capture",    "Capture",           buildCapture         },
        }},
        { "levelinfo", "Level Info", {
            { "levelinfo",  "Level Info Screen",   buildLevelInfo  },
            { "interface",  "Interface & Popups",  buildInterface  },
        }},
        { "audio", "Audio", {
            { "music",       "Profile Music", buildProfileMusic },
            { "globalmusic", "Music Layers",  buildGlobalMusic  },
        }},
        { "backgrounds", "Backgrounds", {
            { "backgrounds", "Per-Layer Backgrounds", buildBackgrounds },
            { "transitions", "Transitions",           buildTransitions },
        }},
        { "extras", "Pet & More", {
            { "pet",         "Pet",           buildPet         },
            { "cursor",      "Custom Cursor", buildCursor      },
            { "menuphysics", "Menu Physics",  buildMenuPhysics },
            { "scorecells",  "Score Cells",   buildScoreCells  },
            { "performance", "Performance",   buildPerformance },
        }},
        { "features", "Features", {
            { "profileredesign", "Profile Redesign", buildProfileRedesign },
            { "autopreview",     "Auto Previews",    buildAutoPreview     },
            { "texturestudio",   "Texture Studio",   buildTextureStudio   },
            { "songsearch",      "Song Search",      buildSongSearch      },
        }},
        { "discord", "Discord", {
            { "discord", "Rich Presence", buildDiscord },
        }},
    };
    return s_groups;
}

// Flat category list (legacy)
std::vector<SettingsCategory> const& getAllCategories() {
    static std::vector<SettingsCategory> s_categories = {
        { "general",       "General",       "", buildGeneral       },
        { "thumbnails",    "Thumbnails",    "", buildLevelThumbnails},
        { "vfx",           "Visual FX",     "", buildVisualEffects  },
        { "levelinfo",     "Level Info",    "", buildLevelInfo      },
        { "music",         "Profile Music", "", buildProfileMusic   },
        { "capture",       "Capture",       "", buildCapture        },
        { "performance",   "Performance",   "", buildPerformance    },
        { "interface",     "Interface",     "", buildInterface      },
        { "backgrounds",   "Backgrounds",   "", buildBackgrounds    },
        { "transitions",   "Transitions",   "", buildTransitions    },
        { "pet",           "Pet",           "", buildPet            },
        { "cursor",        "Custom Cursor", "", buildCursor         },
        { "menuphysics",   "Menu Physics",  "", buildMenuPhysics    },
        { "profileredesign","Profile Redesign","", buildProfileRedesign },
        { "autopreview",   "Auto Previews", "", buildAutoPreview    },
        { "texturestudio", "Texture Studio","", buildTextureStudio  },
        { "songsearch",    "Song Search",   "", buildSongSearch     },
        { "scorecells",    "Score Cells",   "", buildScoreCells     },
        { "globalmusic",   "Music Layers",  "", buildGlobalMusic    },
        { "discord",       "Discord RPC",   "", buildDiscord        },
        { "maintenance",   "Maintenance",   "", buildMaintenance    },
    };
    return s_categories;
}

} // namespace paimon::settings_ui
