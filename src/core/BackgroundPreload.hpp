#pragma once

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include "PreloadProgress.hpp"

namespace paimon::preload {

// speculative work yields to gameplay/editor; real ui requests keep priority.
inline bool canRunBackgroundPreload() {
    return g_gameLoaded.load(std::memory_order_acquire)
        && !PlayLayer::get() && !LevelEditorLayer::get();
}

} // namespace paimon::preload
