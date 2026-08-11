#pragma once
// Capa de red de la tienda: baja registry.json y los .gdicon, y desempaqueta
// el ZIP en las piezas que nos interesan. Todo callback vuelve en el hilo
// principal (WebHelper lo garantiza).

#include "../IconGalleryTypes.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::icon_gallery {

// Contenido util de un .gdicon ya desempaquetado.
struct GalleryPackage {
    GalleryIcon meta;                    // de icon.json (metaLoaded = true)
    std::vector<std::uint8_t> preview;   // preview.png
    std::vector<std::uint8_t> sheetPng;  // <algo>-uhd.png
    std::vector<std::uint8_t> plist;     // <algo>-uhd.plist
    std::string sheetPngName;            // nombre original dentro del ZIP
    std::string plistName;
};

class GalleryClient final {
public:
    using RegistryCallback =
        geode::CopyableFunction<void(geode::Result<std::vector<std::string>>)>;
    using PackageCallback =
        geode::CopyableFunction<void(geode::Result<GalleryPackage>)>;

    // GET /icons/registry.json -> rutas ("icons/Wumpus.gdicon").
    static void fetchRegistry(RegistryCallback cb);

    // GET <base>/<icon.path> y desempaqueta. `slug` solo se usa para los
    // mensajes de error.
    static void fetchPackage(GalleryIcon const& icon, PackageCallback cb);

    // Desempaqueta bytes de un .gdicon ya descargado (o leido de la cache).
    static geode::Result<GalleryPackage> unpack(std::vector<std::uint8_t> const& bytes,
                                                std::string const& slug);

    // Lee icon.json. Publico porque la cache en disco guarda ese JSON tal cual.
    static geode::Result<GalleryIcon> parseMeta(std::string const& json,
                                                std::string const& slug);

private:
    GalleryClient() = delete;
};

}  // namespace paimon::icon_gallery
