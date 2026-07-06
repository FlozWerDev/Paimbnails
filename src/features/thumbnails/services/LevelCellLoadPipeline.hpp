#pragma once

#include <Geode/Geode.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include "LevelCellState.hpp"
#include "ThumbnailLoader.hpp"
#include "../../../utils/RetainedLazyTextureLoad.hpp"

namespace paimon::thumbnails::levelcell {

struct ThumbnailLoadInput {
    int32_t levelID = 0;
    int dailyID = 0;
    bool isOnScreen = true;
};

struct ThumbnailLoadFieldRefs {
    bool& isBeingDestroyed;
    bool& thumbnailRequested;
    bool& thumbnailApplied;
    bool& thumbnailFailed;
    int32_t& lastRequestedLevelID;
    int& requestId;
    int& loadedInvalidationVersion;
    bool& hasGif;
    geode::Ref<cocos2d::CCSprite>& thumbSprite;
    geode::Ref<cocos2d::CCTexture2D>& staticTexture;
    paimon::image::RetainedLazyTextureLoad& staticThumbLoad;
    std::chrono::steady_clock::time_point& thumbnailRequestTime;
    ThumbLoadResetFields resetBundle;
};

struct ThumbnailLoadOps {
    geode::CopyableFunction<void()> ensureMaintenance;
    geode::CopyableFunction<void()> ensureInvalidationListener;
    geode::CopyableFunction<void(int32_t)> resetForLevelChange;
    geode::CopyableFunction<bool(int32_t)> tryReuseApplied;
    geode::CopyableFunction<void(int32_t, int, cocos2d::CCTexture2D*, bool)> applyStatic;
    geode::CopyableFunction<void(int32_t)> requestGallery;
    geode::CopyableFunction<bool(int32_t, bool)> tryLocalVideo;
    geode::CopyableFunction<bool(int32_t, int, bool)> tryServerVideo;
    geode::CopyableFunction<void(int32_t, int, bool, bool)> requestStatic;
};

void runThumbnailLoadPipeline(
    ThumbnailLoadInput const& input,
    ThumbnailLoadFieldRefs& fields,
    ThumbnailLoadOps const& ops
);

} // namespace paimon::thumbnails::levelcell