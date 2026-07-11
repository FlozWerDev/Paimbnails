#include "LevelCellVideoLoad.hpp"
#include "LevelCellThumbnailLoad.hpp"
#include "LocalThumbs.hpp"
#include "../../../video/VideoPlayer.hpp"
#include "../../../utils/Debug.hpp"
#include <Geode/utils/string.hpp>
#include <utility>

using namespace geode::prelude;

namespace paimon::thumbnails::levelcell {

bool tryStartLocalVideoThumbnail(int32_t levelID, bool enableSpinners, LocalVideoHost const& host) {
    auto localPath = LocalThumbs::get().findAnyThumbnail(levelID);
    if (!localPath) return false;

    auto const lowerPath = geode::utils::string::toLower(*localPath);
    if (!lowerPath.ends_with(".mp4")) return false;

    PaimonDebug::log("[LevelCell] tryLoadThumbnail: found MP4 for levelID={}: {}", levelID, *localPath);

    auto player = paimon::video::VideoPlayer::create(*localPath);
    if (!player) {
        log::warn("[LevelCell] tryLoadThumbnail: MP4 player creation failed for levelID={}", levelID);
        return false;
    }

    player->setLoop(true);
    player->setVolume(0.0f);
    // Decode starts on play(); GPU pipeline init is deferred inside play().
    player->play();

    if (host.setHasVideo) host.setHasVideo(true);
    if (host.storePlayer) host.storePlayer(std::move(player));
    if (host.refreshScheduling) host.refreshScheduling();

    if (host.getPlayer) {
        if (auto* live = host.getPlayer(); live && live->hasVisibleFrame()) {
            if (host.setThumbnailApplied) host.setThumbnailApplied(true);
            if (enableSpinners && host.hideSpinner) host.hideSpinner();
            if (host.applyThumbTexture) host.applyThumbTexture(live->getCurrentFrameTexture());
            PaimonDebug::log("[LevelCell] tryLoadThumbnail: video player started for levelID={}", levelID);
            return true;
        }
    }

    PaimonDebug::log("[LevelCell] tryLoadThumbnail: waiting for first MP4 frame for levelID={}", levelID);
    return true;
}

bool tryStartServerVideoThumbnail(
    int32_t levelID,
    int requestId,
    bool enableSpinners,
    ThumbnailAPI::ThumbnailInfo const& mainThumb,
    geode::WeakRef<CCNode> cellAnchor,
    std::weak_ptr<std::monostate> cancelToken,
    ServerVideoHost const& host
) {
    if (!mainThumb.isVideo() || mainThumb.url.empty()) return false;

    PaimonDebug::log("[LevelCell] tryLoadThumbnail: main thumb is server video for levelID={}", levelID);

    std::string const cacheKey = fmt::format("thumb_video_{}", levelID);

    // Disk / warm-player hit: skip spinner flash for already-local assets.
    bool const likelyCached = VideoThumbnailSprite::isCached(cacheKey);
    if (enableSpinners && !likelyCached && host.showSpinner) host.showSpinner();

    VideoThumbnailSprite::createAsync(mainThumb.url, cacheKey, [
        cellAnchor,
        cancelToken,
        levelID,
        requestId,
        enableSpinners,
        host
    ](VideoThumbnailSprite* videoSprite) {
        if (isAsyncCancelled(cancelToken)) return;

        auto cellRef = cellAnchor.lock();
        if (!cellRef || !cellRef->getParent()) return;
        if (host.shouldHandle && !host.shouldHandle(levelID, requestId)) return;

        if (!videoSprite) {
            log::warn("[LevelCell] tryLoadThumbnail: server video creation failed for levelID={}", levelID);
            if (host.retryThumbnailLoad) host.retryThumbnailLoad();
            return;
        }

        videoSprite->setVolume(0.0f);
        videoSprite->setLoop(true);
        videoSprite->setVisible(false);
        videoSprite->setID("video-driver-pending"_spr);
        if (host.attachPendingDriver) host.attachPendingDriver(videoSprite);

        videoSprite->setOnFirstVisibleFrame([
            cellAnchor,
            cancelToken,
            levelID,
            requestId,
            enableSpinners,
            host
        ](VideoThumbnailSprite* readySprite) {
            if (isAsyncCancelled(cancelToken)) {
                if (readySprite->getParent()) readySprite->removeFromParent();
                return;
            }

            auto innerRef = cellAnchor.lock();
            if (!innerRef || !innerRef->getParent()) {
                if (readySprite->getParent()) readySprite->removeFromParent();
                return;
            }
            if (host.shouldHandle && !host.shouldHandle(levelID, requestId)) {
                if (readySprite->getParent()) readySprite->removeFromParent();
                return;
            }

            if (auto* tex = readySprite->getTexture()) {
                if (host.setHasVideo) host.setHasVideo(true);
                if (host.setThumbnailApplied) host.setThumbnailApplied(true);
                if (enableSpinners && host.hideSpinner) host.hideSpinner();
                if (host.applyThumb) host.applyThumb(tex, readySprite);

                if (host.galleryThumbCount && host.galleryThumbCount() > 1) {
                    if (host.setGalleryIndex) host.setGalleryIndex(0);
                    if (host.galleryAutocycle && host.galleryAutocycle() && host.scheduleGalleryCycle) {
                        host.scheduleGalleryCycle();
                    }
                }

                PaimonDebug::log(
                    "[LevelCell] tryLoadThumbnail: server video first frame ready for levelID={}",
                    levelID
                );
            }
        });
        videoSprite->play();
    });

    return true;
}

} // namespace paimon::thumbnails::levelcell