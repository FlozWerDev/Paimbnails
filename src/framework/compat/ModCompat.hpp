#pragma once

// ModCompat.hpp — Deteccion de mods de texturas activos para evitar conflictos.
// Cachea el resultado al primer uso (los mods no se cargan/descargan en runtime).

#include <Geode/loader/Loader.hpp>

namespace paimon::compat {

struct ModCompat {
    static bool isHappyTexturesLoaded() {
        static bool cached = geode::Loader::get()->isModLoaded("alphalaneous.happy_textures");
        return cached;
    }

    static bool isTextureLdrLoaded() {
        static bool cached = geode::Loader::get()->isModLoaded("geode.texture-loader");
        return cached;
    }

    static bool isImagePlusLoaded() {
        static bool cached = geode::Loader::get()->isModLoaded("prevter.imageplus");
        return cached;
    }

    // true si cualquier mod de texturas esta activo
    static bool anyTextureModLoaded() {
        return isHappyTexturesLoaded() || isTextureLdrLoaded() || isImagePlusLoaded();
    }

    static bool isLevelTagsLoaded() {
        static bool cached = geode::Loader::get()->isModLoaded("kampwski.level_tags");
        return cached;
    }
};

} // namespace paimon::compat
