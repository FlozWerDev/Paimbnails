#include "LevelCellLoadPipeline.hpp"
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

namespace paimon::thumbnails::levelcell {

void runThumbnailLoadPipeline(
    ThumbnailLoadInput const& input,
    ThumbnailLoadFieldRefs& fields,
    ThumbnailLoadOps const& ops
) {
    ops.ensureMaintenance();

    if (input.dailyID > 0) {
        log::debug("[LevelCell] tryLoadThumbnail: skipping daily cell dailyID={}", input.dailyID);
        return;
    }

    if (input.levelID <= 0) {
        log::debug("[LevelCell] tryLoadThumbnail: invalid levelID={}", input.levelID);
        return;
    }

    log::debug("[LevelCell] tryLoadThumbnail: levelID={} onScreen={}", input.levelID, input.isOnScreen);

    fields.isBeingDestroyed = false;
    if (fields.thumbnailApplied && (!fields.thumbSprite || !fields.thumbSprite->getParent())) {
        fields.thumbnailApplied = false;
        fields.thumbnailRequested = false;
    }

    ops.ensureInvalidationListener();

    if (fields.lastRequestedLevelID != input.levelID) {
        ops.resetForLevelChange(input.levelID);
    }

    int const currentVersion = ThumbnailLoader::get().getInvalidationVersion(input.levelID);
    if (fields.thumbnailApplied && currentVersion != fields.loadedInvalidationVersion) {
        log::debug(
            "[LevelCell] tryLoadThumbnail: thumbnail invalidated for levelID={}, ver {} -> {}",
            input.levelID,
            fields.loadedInvalidationVersion,
            currentVersion
        );
        invalidateForRemoteThumbnail(fields.resetBundle, currentVersion);
        fields.hasGif = false;
        fields.staticTexture = nullptr;
        fields.staticThumbLoad.reset();
    }
    fields.loadedInvalidationVersion = currentVersion;

    if (ops.tryReuseApplied(input.levelID)) {
        return;
    }

    if (auto* cachedTex = ThumbnailLoader::get().tryGetCachedTexture(input.levelID, false)) {
        if (ThumbnailLoader::isTextureSane(cachedTex)) {
            fields.requestId++;
            int const cachedRequestId = fields.requestId;
            fields.thumbnailRequested = true;
            fields.lastRequestedLevelID = input.levelID;
            fields.thumbnailFailed = false;
            bool const enableSpinners = true;
            ops.applyStatic(input.levelID, cachedRequestId, cachedTex, enableSpinners);
            ops.requestGallery(input.levelID);
            return;
        }
    }

    ops.requestGallery(input.levelID);

    if (fields.thumbnailRequested) {
        log::debug("[LevelCell] tryLoadThumbnail: already requested for levelID={}", input.levelID);
        return;
    }

    fields.requestId++;
    int const currentRequestId = fields.requestId;
    fields.thumbnailRequested = true;
    fields.thumbnailRequestTime = std::chrono::steady_clock::now();
    fields.lastRequestedLevelID = input.levelID;
    fields.hasGif = ThumbnailLoader::get().hasGIFData(input.levelID);

    bool const enableSpinners = true;

    if (ops.tryLocalVideo(input.levelID, enableSpinners)) {
        return;
    }

    if (ops.tryServerVideo(input.levelID, currentRequestId, enableSpinners)) {
        return;
    }

    ops.requestStatic(input.levelID, currentRequestId, enableSpinners, input.isOnScreen);
}

} // namespace paimon::thumbnails::levelcell