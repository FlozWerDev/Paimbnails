// main.cpp — Entrypoint minimo.
// La logica de arranque y cierre se ha movido a:
//   - src/core/Bootstrap.cpp      (PaimonOnModLoaded, inicializacion diferida)
//   - src/core/RuntimeLifecycle.cpp ($on_game(Exiting), cleanupDiskCache)
//
// Este archivo se conserva casi vacio para no romper commits historicos.
// CMakeLists usa GLOB_RECURSE src/*.cpp, asi que los nuevos archivos
// se compilan automaticamente.

#include "features/discord-presence/services/DiscordPresenceManager.hpp"
#include "utils/MainThread.hpp"

$on_mod(Loaded) {
    // Capturar el main thread ID lo mas temprano posible. $on_mod(Loaded) corre
    // en main thread (Geode lo garantiza), asi que aqui estamos seguros.
    // paimon::isMainThread() despues de este punto sera coherente.
    paimon::captureMainThread();

    paimon::discord::DiscordPresenceManager::get().refreshSoon();
}
