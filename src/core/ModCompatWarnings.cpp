#include "ModCompatWarnings.hpp"
#include "../framework/compat/ModCompat.hpp"
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

void PaimonLogModCompatWarnings() {
    using paimon::compat::ModCompat;

    if (ModCompat::isCDCLevelThumbnailsLoaded()) {
        log::info(
            "[Paimbnails] Level Thumbnails detectado: los dos mods pueden convivir, "
            "cada uno pone su miniatura donde le corresponde."
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


    if (ModCompat::needsConservativeGameplayCapture()) {
        log::info(
            "[Paimbnails] Renderer/capturador externo detectado: las capturas de gameplay "
            "usan el back-buffer compatible y evitan reentrar PlayLayer::visit."
        );
    }

    if (ModCompat::isBetterEditLoaded() || ModCompat::isEditorTabApiLoaded() ||
        ModCompat::isEditorCollabLoaded() || ModCompat::isTinkerLoaded()) {
        log::info(
            "[Paimbnails] Ecosistema de editor externo detectado: los hooks de Collab "
            "permanecen pasivos fuera de una sala conectada."
        );
    }
}
