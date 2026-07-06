#pragma once

#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <functional>
#include <memory>
#include <utility>
#include "ThumbnailLoader.hpp"

namespace paimon::thumbnails::levelcell {

using StaticThumbLoadedFn = geode::CopyableFunction<
    void(cocos2d::CCTexture2D* texture, bool success)
>;

struct StaticThumbRequest {
    int32_t levelID = 0;
    int requestId = 0;
    int invalidationVersion = 0;
    bool enableSpinners = false;
    bool isOnScreen = true;
    std::shared_ptr<std::monostate> cancelToken;
};

void configureLoaderFromSettingsOnce();

bool isAsyncCancelled(std::weak_ptr<std::monostate> const& token);

void requestStaticThumbnail(
    StaticThumbRequest const& req,
    geode::WeakRef<cocos2d::CCNode> cellAnchor,
    StaticThumbLoadedFn onLoaded
);

} // namespace paimon::thumbnails::levelcell