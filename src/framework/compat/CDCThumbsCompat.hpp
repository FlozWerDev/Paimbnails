#pragma once

#include "ModCompat.hpp"
#include <Geode/Geode.hpp>

namespace paimon::compat {

// cdc.level_thumbnails draws its own thumbnail on LevelCell and its own
// background on LevelInfoLayer (node IDs below, in its _spr namespace).
// when Paimbnails is showing its own thumbnail, the CDC nodes are hidden
// so Paimbnails replaces them instead of fighting over the same z-order.
// nodes are hidden, never removed: CDC keeps raw pointers to them in its
// cell fields, and freeing them would leave those dangling.

inline void hideCDCLevelCellThumbs(cocos2d::CCNode* cell) {
    if (!cell || !ModCompat::isCDCLevelThumbnailsLoaded()) return;
    for (char const* id : {
        "cdc.level_thumbnails/clipping-node",
        "cdc.level_thumbnails/separator",
        "cdc.level_thumbnails/download-progress",
        "cdc.level_thumbnails/spinner",
    }) {
        if (auto* node = cell->getChildByID(id)) {
            node->setVisible(false);
        }
    }
}

inline void hideCDCLevelInfoBackground(cocos2d::CCNode* layer) {
    if (!layer || !ModCompat::isCDCLevelThumbnailsLoaded()) return;
    if (auto* node = layer->getChildByID("cdc.level_thumbnails/level-bg")) {
        node->setVisible(false);
    }
}

} // namespace paimon::compat
