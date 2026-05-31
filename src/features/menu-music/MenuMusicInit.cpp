// MenuMusicInit — bootstrap del sistema MenuMusic.
// Sólo carga la libreria desde disco al arranque; el resto de componentes
// son singletons lazy inicializados bajo demanda.

#include "services/MenuMusicLibrary.hpp"
#include "services/MenuMusicPlayer.hpp"
#include <Geode/Geode.hpp>
#include <thread>

#include "../../utils/ThreadTracker.hpp"
#include "../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace paimon::menumusic;

$on_mod(Loaded) {
    // Carga la libreria de musica en background para no bloquear el arranque.
    // El player es lazy y no se usa hasta que el usuario abre el popup de
    // musica, asi que no hay race condition con el main thread.
    paimon::ThreadTracker::get().spawn([]() {
        geode::utils::thread::setName("PaimonMenuMusicLoad");
        if (paimon::isRuntimeShuttingDown()) return;
        MenuMusicLibrary::get().load();
        if (paimon::isRuntimeShuttingDown()) return;
        Loader::get()->queueInMainThread([]() {
            if (paimon::isRuntimeShuttingDown()) return;
            log::info("[MenuMusic] ready — {} tracks, {} playlists",
                MenuMusicLibrary::get().tracks().size(),
                MenuMusicLibrary::get().playlists().size());
        });
    });
}
