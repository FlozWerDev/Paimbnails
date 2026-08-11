#pragma once
// Modelos de la galería. registry.json lista rutas; cada .gdicon contiene la
// hoja, icon.json y preview.png. GalleryStore carga y cachea solo lo visible.

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::icon_gallery {

// Host sin barra final.
constexpr char const* kGalleryBaseUrl = "https://iconsgallery.pages.dev";

// Gamemodes publicados, en orden de UI.
constexpr IconType kGamemodes[] = {
    IconType::Cube, IconType::Ship, IconType::Ball, IconType::Ufo,
    IconType::Wave, IconType::Robot, IconType::Spider, IconType::Swing,
    IconType::Jetpack,
};

// Convierte el nombre de gamemode; devuelve false si no es válido.
bool iconTypeFromName(std::string_view name, IconType& out);

// Nombre usado por la galería (Ufo -> "UFO").
char const* iconTypeName(IconType type);

std::string iconTypeLabel(IconType type);

struct GalleryIcon {
    // Siempre disponible desde registry.json.
    std::string slug;
    std::string path;

    // Se completa al cargar icon.json.
    bool metaLoaded = false;
    std::string name;
    std::string author;
    std::string description;
    std::string format;       // "vanilla" o "more-icons".
    std::string uuid;
    IconType type = IconType::Cube;
    bool isCollab = false;
    std::vector<std::string> collabWith;
    std::int64_t createdAtMs = 0;
    bool hasProjectFiles = false;

    // Colores sugeridos por el autor, si existen.
    bool hasColors = false;
    cocos2d::ccColor3B color1{255, 255, 255};
    cocos2d::ccColor3B color2{255, 255, 255};
    cocos2d::ccColor3B colorGlow{255, 255, 255};

    // Nombre visible; usa el slug mientras faltan metadatos.
    std::string displayName() const;

    std::string url() const;
};

enum class GallerySort {
    Newest,
    Oldest,
    NameAsc,
    AuthorAsc,
};

}
