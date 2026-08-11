#include "services/MenuMusicLibrary.hpp"
#include "services/MenuMusicEffects.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace paimon::menumusic;

$on_mod(Loaded) {
    MenuMusicLibrary::get().load();
    MenuMusicEffects::get().loadConfig();
    log::info("[MenuMusic] ready - {} tracks, {} playlists",
        MenuMusicLibrary::get().tracks().size(),
        MenuMusicLibrary::get().playlists().size());
}
