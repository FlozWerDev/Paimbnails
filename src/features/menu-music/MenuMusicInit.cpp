
#include "services/MenuMusicLibrary.hpp"
#include "services/MenuMusicPlayer.hpp"
#include <Geode/Geode.hpp>
#include <thread>

#include "../../utils/ThreadTracker.hpp"
#include "../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace paimon::menumusic;

$on_mod(Loaded) {
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
