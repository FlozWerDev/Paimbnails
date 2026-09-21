#pragma once
#include <Geode/Geode.hpp>
#include <cctype>
#include <string>
#include <vector>
#include <functional>

namespace paimon::quickhub {

// Discord RPC solo en escritorio; en movil se ocultan sin romper ids.
inline bool discordSupported() {
    auto* mod = geode::Mod::get();
    return mod && mod->hasSetting("discord-rpc-enabled");
}

struct RadialOptionDef {
    std::string id;
    std::string name;
    std::string icon;
    cocos2d::ccColor3B color; // glow en hover
    bool custom = false;     // de la UI del juego
    // Accion resuelta en runtime por id.
    std::string imagePath;   // "" = usar icon
    float imageScale = 1.f;  // clamp 0.2..3.0
    float imageRotation = 0.f; // clamp -180..180
    bool imageFlipX = false;
    bool imageFlipY = false;
};

enum class RadialButtonShape { Circle, Square, Icon };

// int en disco para unwrapOr trivial.
enum class QuickButtonSfxKind : int { None = 0, Game = 1, File = 2, Online = 3 };

// Direccion del boton original para reencontrarlo.
struct CustomQuickButton {
    std::string id;
    std::string name;
    std::string icon;
    std::string labelText;
    std::string targetNodeId;
    std::string parentId;
    std::vector<int> nodePath; // fallback si no hay ids
    std::vector<std::string> idPath;
    std::string ownerClass;
    std::string sceneClass;
    std::string itemClass;
    std::string listenerClass;
    float relX = -1.f;         // normalizada 0..1
    float relY = -1.f;
    int tag = 0;
    cocos2d::ccColor3B color{120, 200, 255};
    RadialButtonShape shape = RadialButtonShape::Circle;
    // Defaults = comportamiento anterior
    std::string imagePath;              // "" = usar icon
    float imageScale = 1.f;             // 0.2..3.0
    float imageRotation = 0.f;          // -180..180
    bool imageFlipX = false;
    bool imageFlipY = false;
    // 0 = sin SFX
    int sfxKind = 0;
    std::string sfxPath;                // Game: nombre; File: ruta; Online: usar sfxId
    int sfxId = 0;
    float sfxVolume = 1.f;              // 0..1
    float sfxSpeed = 1.f;               // 0.4..2.5
    int sfxStartMs = 0;
    int sfxEndMs = 0;                   // 0 = hasta el fin
    int sfxFadeInMs = 0;
    int sfxFadeOutMs = 0;
};

// "Mi Boton!" -> "mi-boton"; vacio -> "button".
inline std::string slugify(std::string const& id) {
    std::string stem;
    for (char c : id) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            stem.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == '-' || c == '_' || c == ' ') {
            stem.push_back('-');
        }
    }
    while (!stem.empty() && stem.front() == '-') stem.erase(stem.begin());
    while (!stem.empty() && stem.back() == '-') stem.pop_back();
    if (stem.empty()) stem = "button";
    if (stem.size() > 40) stem.resize(40);
    return stem;
}

inline RadialOptionDef toRadialDef(CustomQuickButton const& b) {
    RadialOptionDef def;
    def.id = b.id;
    def.name = b.name.empty() ? "Boton rapido" : b.name;
    def.icon = b.icon.empty() ? "GJ_optionsBtn_001.png" : b.icon;
    def.color = b.color;
    def.custom = true;
    def.imagePath = b.imagePath;
    def.imageScale = b.imageScale;
    def.imageRotation = b.imageRotation;
    def.imageFlipX = b.imageFlipX;
    def.imageFlipY = b.imageFlipY;
    return def;
}

inline std::string friendlyScreenName(std::string const& cls) {
    if (cls.empty()) return "esta pantalla";
    if (cls == "MenuLayer")           return "Menu principal";
    if (cls == "CreatorLayer")        return "Creator";
    if (cls == "GJGarageLayer")       return "Garage";
    if (cls == "LevelSelectLayer")    return "Niveles principales";
    if (cls == "GauntletSelectLayer") return "Gauntlets";
    if (cls == "LevelBrowserLayer")   return "Buscador de niveles";
    if (cls == "LevelSearchLayer")    return "Busqueda";
    if (cls == "LevelInfoLayer")      return "Info del nivel";
    if (cls == "EditLevelLayer")      return "Mis niveles";
    if (cls == "LeaderboardsLayer")   return "Leaderboards";
    if (cls == "LevelEditorLayer")    return "Editor";
    if (cls == "EditorUI")            return "Editor";
    if (cls == "PlayLayer")           return "Juego";
    if (cls == "SecretLayer")         return "Sala secreta";
    if (cls == "GJShopLayer")         return "Tienda";
    if (cls == "ProfilePage")         return "Perfil";
    return cls;
}

inline bool isNavigableScreen(std::string const& cls) {
    return cls == "MenuLayer" || cls == "CreatorLayer" || cls == "GJGarageLayer" ||
           cls == "LevelSelectLayer" || cls == "GauntletSelectLayer";
}

// Nuevas opciones aparecen solas en el config.
inline std::vector<RadialOptionDef> getAllAvailableOptions() {
    std::vector<RadialOptionDef> opts = {
        {"settings-general",     "General",          "GJ_optionsBtn_001.png",     {120, 255, 120}},
        {"settings-thumbnails",  "Miniaturas",       "GJ_hammerIcon_001.png",     {100, 200, 255}},
        {"settings-levelinfo",   "Nivel",            "GJ_infoBtn_001.png",        {180, 220, 255}},
        {"settings-audio",       "Audio",            "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"settings-backgrounds", "Fondos",           "GJ_paintBtn_001.png",       {180, 255, 140}},
        {"settings-extras",      "Extras",           "GJ_starBtn_001.png",        {255, 120, 120}},
    };
    if (discordSupported()) {
        opts.push_back({"settings-discord",     "Discord",          "GJ_chatBtn_001.png",        {110, 150, 255}});
    }

    opts.insert(opts.end(), {
        {"transitions",          "Transiciones",     "GJ_replayBtn_001.png",      {200, 160, 255}},
    });
    if (discordSupported()) {
        opts.push_back({"discord-config",       "Discord Config",   "GJ_chatBtn_001.png",        {110, 150, 255}});
    }
    opts.insert(opts.end(), {
        {"pet-config",           "Mascota",          "gj_heartOn_001.png",        {255, 180, 200}},
        {"cursor-config",        "Cursor",           "GJ_searchBtn_001.png",      {255, 200, 120}},
        {"slider-config",        "Slider",           "GJ_optionsBtn_001.png",     {160, 255, 220}},
        {"progressbar-config",   "Barra Progreso",   "GJ_arrow_03_001.png",       {255, 220, 130}},
        {"profile-pic-editor",   "Foto Perfil",      "GJ_profileButton_001.png",  {180, 220, 255}},

        {"menu-music",           "Menu Music",       "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"menu-music-library",   "Music Library",    "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"menu-music-playlists", "Playlists",        "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"profile-music",        "Profile Music",    "GJ_musicOnBtn_001.png",     {220, 160, 255}},

        {"pet-shop",             "Tienda Paimon",    "GJ_storeBtn_001.png",       {255, 220, 100}},

        {"hub",                  "Paimon Hub",       "GJ_menuBtn_001.png",        {255, 220, 130}},
        {"paidraw",              "PaiDraw",          "GJ_creatorBtn_001.png",     {255, 200, 160}},
        {"support",              "Soporte",          "GJ_infoBtn_001.png",        {255, 180, 120}},
        {"full-config",          "Editor Fondos",    "GJ_paintBtn_001.png",       {180, 255, 140}},
    });
    return opts;
}

inline std::vector<std::string> getDefaultRadialOrder() {
    std::vector<std::string> order = {
        "settings-general",
        "settings-thumbnails",
        "settings-audio",
        "full-config",
        "transitions",
        "pet-config",
    };
    if (discordSupported()) {
        order.push_back("discord-config");
    }
    order.push_back("hub");
    return order;
}

constexpr int MAX_RADIAL_OPTIONS = 16;

} // namespace paimon::quickhub
