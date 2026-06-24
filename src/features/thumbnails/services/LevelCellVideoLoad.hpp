#pragma once

#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"

namespace paimon::video {
class VideoPlayer;
}

namespace paimon::thumbnails::levelcell {

struct LocalVideoHost {
    geode::CopyableFunction<void()> refreshScheduling;
    geode::CopyableFunction<void(cocos2d::CCTexture2D* frame)> applyThumbTexture;
    geode::CopyableFunction<void()> hideSpinner;
    geode::CopyableFunction<void(std::unique_ptr<paimon::video::VideoPlayer>)> storePlayer;
    geode::CopyableFunction<paimon::video::VideoPlayer*()> getPlayer;
    geode::CopyableFunction<void(bool)> setHasVideo;
    geode::CopyableFunction<void(bool)> setThumbnailApplied;
};

bool tryStartLocalVideoThumbnail(int32_t levelID, bool enableSpinners, LocalVideoHost const& host);

struct ServerVideoHost {
    geode::CopyableFunction<bool(int32_t levelID, int requestId)> shouldHandle;
    geode::CopyableFunction<void()> showSpinner;
    geode::CopyableFunction<void()> hideSpinner;
    geode::CopyableFunction<void()> retryThumbnailLoad;
    geode::CopyableFunction<void(cocos2d::CCTexture2D*, VideoThumbnailSprite*)> applyThumb;
    geode::CopyableFunction<void(VideoThumbnailSprite*)> attachPendingDriver;
    geode::CopyableFunction<void(bool)> setHasVideo;
    geode::CopyableFunction<void(bool)> setThumbnailApplied;
    geode::CopyableFunction<void(int)> setGalleryIndex;
    geode::CopyableFunction<void()> scheduleGalleryCycle;
    geode::CopyableFunction<size_t()> galleryThumbCount;
    geode::CopyableFunction<bool()> galleryAutocycle;
};

bool tryStartServerVideoThumbnail(
    int32_t levelID,
    int requestId,
    bool enableSpinners,
    ThumbnailAPI::ThumbnailInfo const& mainThumb,
    geode::WeakRef<cocos2d::CCNode> cellAnchor,
    std::weak_ptr<std::monostate> cancelToken,
    ServerVideoHost const& host
);

} // namespace paimon::thumbnails::levelcell