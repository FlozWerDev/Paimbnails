#pragma once
#include <Geode/Geode.hpp>
#include <cctype>
#include <string>
#include <vector>
#include <functional>

namespace paimon::quickhub {

struct RadialOptionDef {
    std::string id;          // identificador unico persistente
    std::string name;        // nombre corto mostrado en hover
    std::string icon;        // sprite frame name del icono
    cocos2d::ccColor3B color; // color del glow en hover
    bool custom = false;     // capturado de la UI del juego
    // La accion se resuelve en runtime por id
    // Skin visual extendida (solo customs; builtins usan defaults).
    std::string imagePath;   // "" = usar frame icon. Ruta absoluta en config.
    float imageScale = 1.f;  // multiplicador sobre caja fitted. clamp 0.2..3.0
    float imageRotation = 0.f; // grados Z. clamp -180..180
    bool imageFlipX = false;
    bool imageFlipY = false;
};

enum class RadialButtonShape { Circle, Square, Icon };

// Fuente del SFX por boton. int en disco para unwrapOr trivial.
enum class QuickButtonSfxKind : int { None = 0, Game = 1, File = 2, Online = 3 };

// Boton personalizado capturado de la UI del juego y anadible al radial.
// Guarda toda la "direccion" del boton original para poder reencontrarlo:
// ruta de node ids, clase de la pantalla, clase del receptor del callback,
// texto, tag y posicion normalizada en pantalla.
struct CustomQuickButton {
    std::string id;
    std::string name;
    std::string icon;          // sprite frame del icono
    std::string labelText;     // texto de identificacion del boton original
    std::string targetNodeId;  // node id del boton original
    std::string parentId;      // node id del padre
    std::vector<int> nodePath; // ruta de indices hasta el nodo (ultimo recurso)
    std::vector<std::string> idPath; // ruta de node ids desde la escena
    std::string ownerClass;    // capa que contiene el boton (MenuLayer, LevelInfoLayer...)
    std::string sceneClass;    // capa principal de la escena al capturarlo
    std::string itemClass;     // clase del CCMenuItem
    std::string listenerClass; // clase que recibe el callback del boton
    float relX = -1.f;         // posicion normalizada 0..1 al capturarlo
    float relY = -1.f;
    int tag = 0;
    cocos2d::ccColor3B color{120, 200, 255};
    RadialButtonShape shape = RadialButtonShape::Circle;
    // --- Skin visual extendida (defaults = comportamiento viejo) ---
    std::string imagePath;              // "" = usar frame icon
    float imageScale = 1.f;             // 0.2..3.0
    float imageRotation = 0.f;          // -180..180 grados Z
    bool imageFlipX = false;
    bool imageFlipY = false;
    // --- SFX por boton (0 = ninguno, sin supresion) ---
    int sfxKind = 0;                    // QuickButtonSfxKind como int
    std::string sfxPath;                // kind Game: "explode_11.ogg". kind File: ruta abs config. kind Online: "" (usar sfxId)
    int sfxId = 0;                      // kind Online: id libreria SFX
    float sfxVolume = 1.f;              // 0..1
    float sfxSpeed = 1.f;               // pitch/speed 0.4..2.5
    int sfxStartMs = 0;                 // >=0
    int sfxEndMs = 0;                   // 0 = hasta el fin
    int sfxFadeInMs = 0;                // >=0
    int sfxFadeOutMs = 0;               // >=0
};

// "Mi Boton!" -> "mi-boton". Vacio o sin alfanumericos -> "button".
// Vive aqui (y no en ui/) porque el manager lo usa para derivar ids.
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

// Vista de dibujo de un CustomQuickButton: copia nombre/icono/color + skin.
// Punto unico de conversion para la rueda, previews y persistencia.
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

// Nombre legible de las pantallas de GD mas comunes.
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

// Pantallas a las que el radial sabe navegar por su cuenta.
inline bool isNavigableScreen(std::string const& cls) {
    return cls == "MenuLayer" || cls == "CreatorLayer" || cls == "GJGarageLayer" ||
           cls == "LevelSelectLayer" || cls == "GauntletSelectLayer";
}

// All available options (full pool). New options appear automatically in RadialConfigPopup.
inline std::vector<RadialOptionDef> getAllAvailableOptions() {
    return {
        // Settings panel: una entrada por categoria
        {"settings-general",     "General",          "GJ_optionsBtn_001.png",     {120, 255, 120}},
        {"settings-thumbnails",  "Miniaturas",       "GJ_hammerIcon_001.png",     {100, 200, 255}},
        {"settings-levelinfo",   "Nivel",            "GJ_infoBtn_001.png",        {180, 220, 255}},
        {"settings-audio",       "Audio",            "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"settings-backgrounds", "Fondos",           "GJ_paintBtn_001.png",       {180, 255, 140}},
        {"settings-extras",      "Extras",           "GJ_starBtn_001.png",        {255, 120, 120}},
        {"settings-discord",     "Discord",          "GJ_chatBtn_001.png",        {110, 150, 255}},

        // Popups de configuracion directa
        {"transitions",          "Transiciones",     "GJ_replayBtn_001.png",      {200, 160, 255}},
        {"discord-config",       "Discord Config",   "GJ_chatBtn_001.png",        {110, 150, 255}},
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
    };
}

// Configuracion por defecto: las opciones mas usadas en orden razonable.
inline std::vector<std::string> getDefaultRadialOrder() {
    return {
        "settings-general",
        "settings-thumbnails",
        "settings-audio",
        "full-config",
        "transitions",
        "pet-config",
        "discord-config",
        "hub",
    };
}

// Max simultaneous radial options (16). Circular layout auto-distributes by angle.
constexpr int MAX_RADIAL_OPTIONS = 16;

} // namespace paimon::quickhub
