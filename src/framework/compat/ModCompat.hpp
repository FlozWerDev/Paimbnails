#pragma once

// ModCompat.hpp — Deteccion de mods de texturas activos para evitar conflictos.
// Consulta el Loader en cada llamada (las funciones son baratas y el resultado
// puede cambiar si el usuario habilita/deshabilita mods en runtime).

#include <Geode/loader/Loader.hpp>

namespace paimon::compat {

struct ModCompat {
    // ── Mods de texturas ─────────────────────────────────────────────
    static bool isHappyTexturesLoaded() {
        return geode::Loader::get()->isModLoaded("alphalaneous.happy_textures");
    }

    static bool isTextureLdrLoaded() {
        return geode::Loader::get()->isModLoaded("geode.texture-loader");
    }

    static bool isImagePlusLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.imageplus");
    }

    // true si cualquier mod de texturas esta activo
    static bool anyTextureModLoaded() {
        return isHappyTexturesLoaded() || isTextureLdrLoaded() || isImagePlusLoaded();
    }

    static bool isLevelTagsLoaded() {
        return geode::Loader::get()->isModLoaded("kampwski.level_tags");
    }

    // ── Mods que potencialmente colisionan con nuestras features ─────
    // Detectamos su presencia para poder ajustar comportamiento (ej:
    // bajar prioridad de nuestros hooks o ceder ownership de un area UI).

    // CompactLists: hace lo mismo que nuestro modo compacto. Si esta
    // activo, deberiamos ceder y dejar que el usuario use el suyo.
    static bool isCompactListsLoaded() {
        return geode::Loader::get()->isModLoaded("cvolton.compactlists-geode");
    }

    // BetterInfo: agrega mucha UI a info layers / level browser. No es
    // incompatible pero hay que evitar pisar sus botones.
    static bool isBetterInfoLoaded() {
        return geode::Loader::get()->isModLoaded("hjfod.betterinfo");
    }

    // EclipseMenu: tiene su propia capa de popups y blur — debemos
    // evitar competir con el suyo.
    static bool isEclipseMenuLoaded() {
        return geode::Loader::get()->isModLoaded("eclipsemenu.eclipse-menu") ||
               geode::Loader::get()->isModLoaded("prevter.eclipsemenu");
    }

    // Globed: agrega popups y RoomPopup que comparte parent con nuestro
    // blur. Hubo un crash documentado al respecto (ver DynamicPopupHook).
    static bool isGlobedLoaded() {
        return geode::Loader::get()->isModLoaded("dankmeme.globed2") ||
               geode::Loader::get()->isModLoaded("dankmeme.globed");
    }

    // Menu Loop Randomizer: comparte dominio con nuestro Menu Music.
    static bool isMenuLoopRandomizerLoaded() {
        return geode::Loader::get()->isModLoaded("fleym.menuloop_randomizer");
    }

    // ── Mods de blur ─────────────────────────────────────────────────
    // Otros mods que aplican blur a popups. Si alguno está aplicando blur
    // activo (no API), evitamos aplicar el nuestro encima para no
    // duplicar FBO passes ni corromper el snapshot de la escena.
    static bool isBlurBGLoaded() {
        // alphalaneous.blur_bg aplica blur a TODOS los popups del juego.
        return geode::Loader::get()->isModLoaded("alphalaneous.blur_bg");
    }
    static bool isBlurAPILoaded() {
        // thesillydoggo.blur-api expone una API; otros mods deciden si
        // aplican blur. No conflicta por sí mismo.
        return geode::Loader::get()->isModLoaded("thesillydoggo.blur-api");
    }
    // True si hay un mod aplicando blur global a popups que ya cubre el
    // caso de uso de nuestro PopupBlurService.
    static bool externalGlobalBlurActive() {
        return isBlurBGLoaded();
    }
};

} // namespace paimon::compat
