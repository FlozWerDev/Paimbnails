#pragma once

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include "PreloadProgress.hpp"

namespace paimon::preload {

// Speculative texture/shader work yields to gameplay and the editor. Actual
// UI requests still load on demand at their normal priority.
inline bool canRunBackgroundPreload() {
    return g_gameLoaded.load(std::memory_order_acquire)
        && !PlayLayer::get() && !LevelEditorLayer::get();
}

} // namespace paimon::preload
