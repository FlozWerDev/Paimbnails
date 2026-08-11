#pragma once
// Instala iconos de la tienda en More Icons.
//
// La galeria entrega hojas ya listas (png + plist con nombres de frame
// vanilla o propios), que es justo lo que More Icons consume, asi que
// "descargar" = escribir la hoja en la carpeta del mod y registrarla.
// Sin More Icons no hay donde meterlas: la tienda avisa y ofrece solo
// guardar los archivos.

#include "../IconGalleryTypes.hpp"
#include "GalleryClient.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::icon_gallery {

class GalleryInstaller final {
public:
    static bool moreIconsAvailable();

    // Nombre con el que queda registrado en More Icons.
    static std::string registeredName(std::string_view slug);

    // Escribe la hoja en installed/<slug>/ y la registra en More Icons.
    // Requiere hilo principal (toca las listas de More Icons).
    static geode::Result<> install(GalleryPackage const& pkg);

    // Solo escribe los archivos, sin registrar. Se usa como salida digna
    // cuando More Icons no esta instalado.
    static geode::Result<std::filesystem::path> saveOnly(GalleryPackage const& pkg);

    // Borra la hoja del disco y la quita de More Icons.
    static geode::Result<> uninstall(std::string const& slug, IconType type);

    // Equipa el icono ya instalado en su gamemode.
    static bool equip(std::string const& slug, IconType type);

    // True si el icono equipado en `type` es este.
    static bool isEquipped(std::string const& slug, IconType type);

    // Vuelve a registrar en More Icons todo lo que hay en installed/.
    // Se llama una vez al abrir la tienda: More Icons no conoce nuestra
    // carpeta, asi que tras reiniciar el juego hay que recordarselo.
    static void registerAllInstalled();

private:
    GalleryInstaller() = delete;
};

}  // namespace paimon::icon_gallery
