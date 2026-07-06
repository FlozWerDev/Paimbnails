#pragma once

#include <Geode/Geode.hpp>
#include <cocos2d.h>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../utils/VideoThumbnailSprite.hpp"
#include "../../../video/VideoPlayer.hpp"
#include "../../../utils/RetainedLazyTextureLoad.hpp"

namespace paimon::thumbnails::levelcell {

struct ThumbLoadResetFields {
    int32_t& lastRequestedLevelID;
    bool& thumbnailRequested;
    bool& thumbnailApplied;
    bool& thumbnailFailed;
    bool& hasGif;
    bool& isDailyCellCached;
    int& loadedInvalidationVersion;
    int& galleryIndex;
    float& galleryTimer;
    bool& galleryRequested;
    int& galleryToken;
    bool& hasVideo;
    std::shared_ptr<std::monostate>& asyncCancelToken;
    geode::Ref<cocos2d::CCTexture2D>& staticTexture;
    paimon::image::RetainedLazyTextureLoad& staticThumbLoad;
    std::vector<ThumbnailAPI::ThumbnailInfo>& galleryThumbnails;
    std::unordered_set<std::string>& galleryPendingUrls;
    std::unique_ptr<paimon::video::VideoPlayer>& videoPlayer;
    geode::Ref<VideoThumbnailSprite>& videoDriver;
};

void stopVideoPlayback(ThumbLoadResetFields& fields);

void resetForLevelChange(ThumbLoadResetFields& fields, int32_t newLevelID);

void invalidateForRemoteThumbnail(ThumbLoadResetFields& fields, int invalidationVersion);

} // namespace paimon::thumbnails::levelcell