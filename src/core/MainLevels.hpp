#pragma once

// MainLevels.hpp — Identificacion y proteccion de los niveles oficiales (1-22).
//
// Los niveles 1-22 son los main levels de Geometry Dash (Stereo Madness ...
// Deadlocked + sub-zero levels). Estos son los unicos thumbnails que el
// servidor sirve garantizado y que no cambian nunca, asi que:
//
//   1. Se prefetchean al arrancar el mod (Bootstrap.cpp) — como hace globed2
//      con los iconos.
//   2. Se preservan en el cache de disco aunque el usuario active la opcion
//      "limpiar cache al salir" o ejecute "Maintenance Cleanup".
//
// Esto garantiza que las miniaturas de los main levels esten siempre listas
// sin tener que volver a descargarlas en cada arranque.

#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <atomic>
#include <filesystem>
#include <string>
#include <utility>

namespace paimon {

// Rango de niveles oficiales de GD (1 = Stereo Madness, 22 = The Tower / sub-zero etc).
inline constexpr int kMainLevelMinID = 1;
inline constexpr int kMainLevelMaxID = 22;

// Devuelve true si <levelID> es un main level oficial.
inline bool isMainLevelID(int levelID) {
    return levelID >= kMainLevelMinID && levelID <= kMainLevelMaxID;
}

// Devuelve true si <filename> corresponde a una miniatura de un main level
// oficial. Acepta solo "<id>.png" o "<id>.gif" donde id ∈ [1, 22].
//
// La cache solo guarda main levels con esos nombres (ver
// paimon::quality::thumbFilename), asi que este check es exacto.
inline bool isMainLevelCacheFile(std::filesystem::path const& filename) {
    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(filename.extension()));
    if (ext != ".png" && ext != ".gif") return false;

    auto stem = geode::utils::string::pathToString(filename.stem());
    auto idResult = geode::utils::numFromString<int>(stem);
    if (!idResult) return false;
    return isMainLevelID(idResult.unwrap());
}

// Borra todo el contenido directo del directorio <cacheDir> excepto:
//   - Sub-directorios cuyo nombre este en <preservedSubdirs> (ej: "gifs").
//   - Archivos de miniaturas de main levels (1-22).
//
// Usar en lugar de std::filesystem::remove_all(cacheDir) cuando se quiera
// limpiar el cache pero conservar los main levels prefetcheados.
//
// Devuelve un par {preservados, eliminados} con la cuenta de cada.
inline std::pair<int, int> clearCachePreservingMainLevels(
    std::filesystem::path const& cacheDir,
    std::initializer_list<std::string_view> preservedSubdirs = {}
) {
    int preserved = 0;
    int removed = 0;

    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        return {preserved, removed};
    }

    std::filesystem::directory_iterator it(cacheDir, ec);
    if (ec) {
        geode::log::warn(
            "[Paimbnails] clearCachePreservingMainLevels: failed to open dir: {}",
            ec.message()
        );
        return {preserved, removed};
    }

    for (auto const& entry : it) {
        std::error_code dummy;
        auto const path = entry.path();
        auto filename = geode::utils::string::pathToString(path.filename());

        // 1) Preservar sub-directorios protegidos (ej: cache/gifs/).
        if (entry.is_directory(dummy)) {
            bool keep = false;
            for (auto const& kept : preservedSubdirs) {
                if (filename == kept) {
                    keep = true;
                    break;
                }
            }
            if (keep) {
                preserved++;
                continue;
            }
        }

        // 2) Preservar miniaturas de main levels (1-22.png/.gif).
        if (entry.is_regular_file(dummy) && isMainLevelCacheFile(path.filename())) {
            preserved++;
            continue;
        }

        // 3) Eliminar el resto.
        std::error_code rmEc;
        std::filesystem::remove_all(path, rmEc);
        if (rmEc) {
            geode::log::warn(
                "[Paimbnails] clearCachePreservingMainLevels: failed to remove {}: {}",
                geode::utils::string::pathToString(path), rmEc.message()
            );
        } else {
            removed++;
        }
    }

    return {preserved, removed};
}

// Bandera global: se setea la primera vez que cualquier capa (LoadingLayer
// o MenuLayer/Bootstrap) dispara el prefetch de los main levels. Evita
// re-encolar trabajo redundante; el ThumbnailLoader ya es idempotente,
// pero esto ahorra logs y la chequeada por entrada.
inline std::atomic<bool> g_mainLevelsPrefetched{false};

// Devuelve true si este caller fue el primero en disparar el prefetch.
// Caller debe usar el resultado para decidir si encolar o no — la primera
// vez devuelve true, las siguientes devuelven false.
inline bool tryClaimMainLevelsPrefetch() {
    bool expected = false;
    return g_mainLevelsPrefetched.compare_exchange_strong(expected, true);
}

} // namespace paimon
