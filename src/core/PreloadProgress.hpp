#pragma once

// PreloadProgress.hpp — Estado global del preload de assets en el arranque.
//
// Inspirado en el PreloadManager de Globed (globed2/src/core/preload/),
// pero adaptado a la realidad de Paimbnails: nuestros assets son thumbnails
// y emotes que se descargan por red, no spritesheets locales. Por eso el
// preload NO bloquea el LoadingLayer (sería demasiado lento) — simplemente
// arranca las descargas en paralelo y publica el progreso a través de
// estos contadores atómicos para que el hook de LoadingLayer pueda
// mostrar un label estilo "Paimbnails: 12/52".
//
// Lifecycle:
//   1. LoadingLayer::init() llama tryClaimPreload() — gana el primer caller.
//   2. El claimer registra el total de thumbnails (22) y arranca su descarga.
//   3. Cada vez que un thumbnail termina (cache hit, descarga, fallo, 404),
//      su callback incrementa g_thumbsLoaded.
//   4. En paralelo, se asegura de tener el catálogo de emotes (disco o
//      red); cuando esté listo, registra el total de emotes y arranca
//      EmoteCache::preloadAllToDisk con un callback per-step que actualiza
//      g_emotesLoaded.
//   5. El LoadingLayer programa una tarea cada 0.1s que lee los contadores
//      y reescribe el label con el total combinado.
//
// Thread-safety:
//   - Todos los contadores son std::atomic<int>.
//   - Los callbacks de ThumbnailLoader y EmoteCache se entregan en el main
//     thread (vía Loader::queueInMainThread), pero usar atomics evita data
//     races si en el futuro alguno se invoca off-thread.

#include <atomic>

namespace paimon::preload {

// ── Thumbnails ──
// Total esperado (normalmente 22). Se establece una sola vez al claim.
inline std::atomic<int> g_thumbsTotal{0};
// Cuántos terminaron (éxito o fallo — el preload no diferencia). Se incrementa
// desde el callback de ThumbnailLoader::requestLoad.
inline std::atomic<int> g_thumbsLoaded{0};

// ── Emotes ──
// Total esperado: tamaño del catálogo de emotes una vez cargado. Mientras
// el catálogo aún no está listo, se queda en 0 y el label muestra
// "cargando catálogo…" en lugar de un X/Y engañoso.
inline std::atomic<int> g_emotesTotal{0};
// Cuántos terminaron (descarga real o skip por estar ya en disco).
inline std::atomic<int> g_emotesLoaded{0};

// True una vez que el catálogo de emotes está disponible (loadCatalogFromDisk
// devolvió o fetchAllEmotes terminó). Útil para distinguir el estado
// "esperando catálogo" del estado "no hay emotes".
inline std::atomic<bool> g_emotesCatalogReady{false};

// Bandera global anti-doble-arranque: el primer LoadingLayer que aparece
// en la sesión se queda con el preload. LoadingLayers posteriores (refresh,
// reload, cambio de tema gráfico) solo observan los contadores.
inline std::atomic<bool> g_preloadStarted{false};

// ── Helpers ──

// Lectura combinada (thumbs + emotes). Se usa para construir "X/Y".
inline int getTotalLoaded() {
    return g_thumbsLoaded.load(std::memory_order_relaxed)
         + g_emotesLoaded.load(std::memory_order_relaxed);
}

inline int getTotalCount() {
    return g_thumbsTotal.load(std::memory_order_relaxed)
         + g_emotesTotal.load(std::memory_order_relaxed);
}

// True si ya hay un total definido y todos los items terminaron.
inline bool isFinished() {
    int total = getTotalCount();
    return total > 0 && getTotalLoaded() >= total;
}

// True si todavía estamos esperando que el catálogo de emotes llegue.
// Mientras esto sea true, g_emotesTotal puede crecer en cualquier momento.
inline bool isWaitingForEmoteCatalog() {
    return !g_emotesCatalogReady.load(std::memory_order_acquire);
}

// Atomically marca este caller como dueño del preload. Devuelve true solo
// la primera vez. Pensado para el patrón:
//
//     if (paimon::preload::tryClaimPreload()) {
//         // soy el primero — arranco las descargas.
//     } else {
//         // ya hay otro LoadingLayer manejándolo — solo observo.
//     }
inline bool tryClaimPreload() {
    bool expected = false;
    return g_preloadStarted.compare_exchange_strong(expected, true);
}

// Reset (solo para tests o re-init manual; el flujo normal nunca lo llama).
inline void resetPreloadState() {
    g_thumbsLoaded.store(0, std::memory_order_relaxed);
    g_thumbsTotal.store(0, std::memory_order_relaxed);
    g_emotesLoaded.store(0, std::memory_order_relaxed);
    g_emotesTotal.store(0, std::memory_order_relaxed);
    g_emotesCatalogReady.store(false, std::memory_order_release);
    g_preloadStarted.store(false, std::memory_order_release);
}

} // namespace paimon::preload
