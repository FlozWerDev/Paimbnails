#include "LevelInfoThumbnailBg.hpp"
#include "ThumbnailLoader.hpp"

using namespace geode::prelude;

namespace paimon::thumbnails::levelinfo {

void requestHeroBackground(
    int levelID,
    Ref<CCNode> layerAnchor,
    HasBackgroundFn hasBackground,
    LevelIdFn currentLevelId,
    ApplyBackgroundFn applyBackground
) {
    if (!layerAnchor || !applyBackground) return;

    if (auto* tex = ThumbnailLoader::get().tryGetCachedTexture(levelID, false)) {
        if (!hasBackground || !hasBackground()) {
            applyBackground(tex);
        }
        return;
    }

    ThumbnailLoader::get().requestLoad(
        levelID,
        fmt::format("{}.png", levelID),
        [layerAnchor, levelID, hasBackground = std::move(hasBackground), currentLevelId = std::move(currentLevelId), applyBackground = std::move(applyBackground)](
            CCTexture2D* tex,
            bool success
        ) {
            if (!layerAnchor->getParent()) return;
            if (currentLevelId && currentLevelId() != levelID) return;
            if (hasBackground && hasBackground()) return;
            if (!success || !tex) return;
            applyBackground(tex);
        },
        ThumbnailLoader::PriorityHero,
        false,
        ThumbnailLoader::Quality::High
    );
}

} // namespace paimon::thumbnails::levelinfo