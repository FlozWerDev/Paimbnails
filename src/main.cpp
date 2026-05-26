// main.cpp — Entrypoint minimo.
// La logica de arranque y cierre se ha movido a:
//   - src/core/Bootstrap.cpp      (PaimonOnModLoaded, inicializacion diferida)
//   - src/core/RuntimeLifecycle.cpp ($on_game(Exiting), cleanupDiskCache)
//
// Este archivo se conserva casi vacio para no romper commits historicos.
// CMakeLists usa GLOB_RECURSE src/*.cpp, asi que los nuevos archivos
// se compilan automaticamente.

#include "features/discord-presence/services/DiscordPresenceManager.hpp"

$on_mod(Loaded) {
    paimon::discord::DiscordPresenceManager::get().refreshSoon();
}
