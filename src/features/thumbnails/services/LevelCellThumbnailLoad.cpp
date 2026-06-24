#include "LevelCellThumbnailLoad.hpp"
#include <Geode/loader/Mod.hpp>

using namespace geode::prelude;

namespace paimon::thumbnails::levelcell {

void configureLoaderFromSettingsOnce() {
    ThumbnailLoader::get().applyConcurrentDownloadsSetting();
}

bool isAsyncCancelled(std::weak_ptr<std::monostate> const& token) {
    return token.expired();
}

void requestStaticThumbnail(
    StaticThumbRequest const& req,
    geode::WeakRef<cocos2d::CCNode> cellAnchor,
    StaticThumbLoadedFn onLoaded
) {
    configureLoaderFromSettingsOnce();

    std::string const fileName = fmt::format("{}.png", req.levelID);
    std::weak_ptr<std::monostate> cancelWeak = req.cancelToken;

    int const priority = req.isOnScreen
        ? ThumbnailLoader::PriorityVisibleCell
        : ThumbnailLoader::PriorityPredictivePrefetch;

    ThumbnailLoader::get().requestLoad(
        req.levelID,
        fileName,
        [cellAnchor, cancelWeak, req, onLoaded = std::move(onLoaded)](
            CCTexture2D* tex,
            bool success
        ) {
            if (isAsyncCancelled(cancelWeak)) return;

            auto cellRef = cellAnchor.lock();
            if (!cellRef || !cellRef->getParent()) return;

            if (!onLoaded) return;
            onLoaded(tex, success);
        },
        priority,
        false
    );
}

} // namespace paimon::thumbnails::levelcell