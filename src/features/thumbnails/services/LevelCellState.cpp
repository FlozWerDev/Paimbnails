#include "LevelCellState.hpp"
#include <Geode/loader/Log.hpp>

namespace paimon::thumbnails::levelcell {

void stopVideoPlayback(ThumbLoadResetFields& fields) {
    if (fields.videoPlayer) {
        fields.videoPlayer->stop();
        fields.videoPlayer.reset();
        fields.hasVideo = false;
    }
    if (fields.videoDriver) {
        if (fields.videoDriver->getParent()) {
            fields.videoDriver->removeFromParent();
        }
        fields.videoDriver = nullptr;
    }
}

void resetForLevelChange(ThumbLoadResetFields& fields, int32_t newLevelID) {
    geode::log::debug(
        "[LevelCell] level changed {} -> {}, resetting thumb state",
        fields.lastRequestedLevelID,
        newLevelID
    );

    fields.thumbnailRequested = false;
    fields.thumbnailApplied = false;
    fields.thumbnailFailed = false;
    fields.lastRequestedLevelID = newLevelID;
    fields.hasGif = false;
    fields.staticTexture = nullptr;
    fields.staticThumbLoad.reset();
    fields.loadedInvalidationVersion = 0;
    fields.isDailyCellCached = false;
    fields.galleryThumbnails.clear();
    fields.galleryPendingUrls.clear();
    fields.galleryIndex = 0;
    fields.galleryTimer = 0.f;
    fields.galleryRequested = false;
    fields.galleryToken++;
    fields.asyncCancelToken = std::make_shared<std::monostate>();
    stopVideoPlayback(fields);
}

void invalidateForRemoteThumbnail(ThumbLoadResetFields& fields, int invalidationVersion) {
    fields.thumbnailRequested = false;
    fields.thumbnailApplied = false;
    fields.thumbnailFailed = false;
    fields.galleryRequested = false;
    fields.galleryThumbnails.clear();
    fields.galleryPendingUrls.clear();
    fields.galleryIndex = -1;
    fields.galleryTimer = 0.f;
    fields.galleryToken++;
    fields.loadedInvalidationVersion = invalidationVersion;
}

} // namespace paimon::thumbnails::levelcell