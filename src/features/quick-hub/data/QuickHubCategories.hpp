#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <functional>

namespace paimon::quickhub {

// Cada opcion del radial
struct RadialOptionDef {
    std::string id;          // identificador unico persistente
    std::string name;        // nombre corto mostrado en hover
    std::string icon;        // sprite frame name del icono
    cocos2d::ccColor3B color; // color del glow en hover
    // La accion se resuelve en runtime por id
};

// Todas las opciones disponibles (pool completo).
// Cualquier opcion nueva agregada aqui se mostrara automaticamente en
// RadialConfigPopup y se podra anadir al circulo. Las acciones se resuelven
// por id en QuickHubRadial::executeOption().
inline std::vector<RadialOptionDef> getAllAvailableOptions() {
    return {
        // ── Settings panel: una entrada por categoria ──
        {"settings-general",     "General",          "GJ_optionsBtn_001.png",     {120, 255, 120}},
        {"settings-thumbnails",  "Miniaturas",       "GJ_hammerIcon_001.png",     {100, 200, 255}},
        {"settings-levelinfo",   "Nivel",            "GJ_infoBtn_001.png",        {180, 220, 255}},
        {"settings-audio",       "Audio",            "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"settings-backgrounds", "Fondos",           "GJ_paintBtn_001.png",       {180, 255, 140}},
        {"settings-extras",      "Extras",           "GJ_starBtn_001.png",        {255, 120, 120}},
        {"settings-discord",     "Discord",          "GJ_chatBtn_001.png",        {110, 150, 255}},

        // ── Popups de configuracion directa ──
        {"backgrounds-editor",   "Editor Fondos",    "GJ_paintBtn_001.png",       {180, 255, 140}},
        {"transitions",          "Transiciones",     "GJ_replayBtn_001.png",      {200, 160, 255}},
        {"discord-config",       "Discord Config",   "GJ_chatBtn_001.png",        {110, 150, 255}},
        {"pet-config",           "Mascota",          "gj_heartOn_001.png",        {255, 180, 200}},
        {"cursor-config",        "Cursor",           "GJ_searchBtn_001.png",      {255, 200, 120}},
        {"slider-config",        "Slider",           "GJ_optionsBtn_001.png",     {160, 255, 220}},
        {"progressbar-config",   "Barra Progreso",   "GJ_arrow_03_001.png",       {255, 220, 130}},
        {"profile-pic-editor",   "Foto Perfil",      "GJ_profileButton_001.png",  {180, 220, 255}},

        // ── Popups de musica ──
        {"menu-music",           "Menu Music",       "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"menu-music-library",   "Music Library",    "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"menu-music-playlists", "Playlists",        "GJ_musicOnBtn_001.png",     {255, 170, 220}},
        {"profile-music",        "Profile Music",    "GJ_musicOnBtn_001.png",     {220, 160, 255}},

        // ── Pet shop ──
        {"pet-shop",             "Tienda Paimon",    "GJ_storeBtn_001.png",       {255, 220, 100}},

        // ── Layers / scenes ──
        {"hub",                  "Paimon Hub",       "GJ_menuBtn_001.png",        {255, 220, 130}},
        {"paidraw",              "PaiDraw",          "GJ_creatorBtn_001.png",     {255, 200, 160}},
        {"support",              "Soporte",          "GJ_infoBtn_001.png",        {255, 180, 120}},
        {"full-config",          "Config Full",      "GJ_optionsBtn_001.png",     {200, 230, 255}},
    };
}

// Configuracion por defecto: las opciones mas usadas en orden razonable.
inline std::vector<std::string> getDefaultRadialOrder() {
    return {
        "settings-general",
        "settings-thumbnails",
        "settings-audio",
        "backgrounds-editor",
        "transitions",
        "pet-config",
        "discord-config",
        "hub",
    };
}

// Maximo de opciones simultaneas en el radial. Subido a 16 para permitir mas
// densidad cuando el usuario quiera meter casi todo. El layout sigue siendo
// circular y se reparte automaticamente por angulo, asi que items grandes
// (>12) empiezan a quedar apretados pero siguen siendo usables.
constexpr int MAX_RADIAL_OPTIONS = 16;

} // namespace paimon::quickhub
