#pragma once
// Compilar + activar un icono. Lo usan el editor ("Usar") y la galeria, asi
// que vive aqui en vez de duplicarse en las dos pantallas.
//
// Threading: llamar desde el hilo principal; el trabajo pesado se va a un
// hilo del ThreadTracker y el callback vuelve al principal.

#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::icon_maker {

class IconBuildService final {
public:
    // On success the message is ready to show to the user ("Listo! ...").
    using DoneCallback = std::function<void(geode::Result<std::string>)>;

    // Compiles the sheets, hands the icon to More Icons when it is installed
    // and to the mod's own applier otherwise, and records the build on disk.
    static void buildAndApply(IconProject project, DoneCallback onDone);

    // Only compiles; leaves the icon un-applied.
    static void build(IconProject project, DoneCallback onDone);

private:
    IconBuildService() = delete;
};

}  // namespace paimon::icon_maker
