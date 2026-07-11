#include "ModCompatWarnings.hpp"
#include "../framework/compat/ModCompat.hpp"
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

void PaimonLogModCompatWarnings() {
    using paimon::compat::ModCompat;

    if (ModCompat::isCDCLevelThumbnailsLoaded()) {
        log::warn(
            "[Paimbnails] Mod 'cdc.level_thumbnails' esta activo: desactivalo para evitar "
            "miniaturas duplicadas o crashes (ver mod.json / popup de inicio)."
        );
    }

    if (ModCompat::isBetterInfoLoaded()) {
        log::info(
            "[Paimbnails] BetterInfo detectado: LevelInfoLayer usa menus validados por ID "
            "para no reclamar botones ajenos."
        );
    }

    if (ModCompat::isCompactListsLoaded()) {
        log::info(
            "[Paimbnails] CompactLists detectado: el swap Level->Level4 lo maneja ese mod."
        );
    }

    if (ModCompat::isCompactPauseMenuLoaded()) {
        log::info(
            "[Paimbnails] Compact Pause Menu detectado: CustomSongWidget corre despues de ese mod."
        );
    }

    if (ModCompat::isMenuLoopRandomizerLoaded()) {
        log::info(
            "[Paimbnails] Menu Loop Randomizer detectado: revisa Menu Music si hay doble control."
        );
    }

    if (ModCompat::isBetterTouchPrioLoaded()) {
        log::info(
            "[Paimbnails] Better Touch Prio detectado: la funcion Z-Order Touch cede el paso a "
            "ese mod (no se hookea touches() por duplicado)."
        );
    }

    if (ModCompat::externalGlobalBlurActive()) {
        log::info(
            "[Paimbnails] Blur global externo (blur_bg): PopupBlurService cede cuando aplica."
        );
    }

    if (ModCompat::isGlobedLoaded() || ModCompat::isEclipseMenuLoaded()) {
        log::info(
            "[Paimbnails] Globed/EclipseMenu detectado: popups dinamicos usan prioridad Late."
        );
    }
}