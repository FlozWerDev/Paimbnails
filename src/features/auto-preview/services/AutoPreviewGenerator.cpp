#include "AutoPreviewGenerator.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>

#include "AutoPreviewStore.hpp"
#include "../AutoPreviewConfig.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/RgbaScaler.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"

using namespace geode::prelude;

namespace paimon::autopreview {

std::unique_ptr<uint8_t[]> downscaleRGBA(
    uint8_t const* src, int srcW, int srcH, int dstW, int dstH) {
    return paimon::rgba::scale(src, srcW, srcH, dstW, dstH);
}

void storeCapturedFrame(int32_t levelID, std::shared_ptr<uint8_t> rgba, int width, int height) {
    if (levelID <= 0 || !rgba || width <= 0 || height <= 0) return;

    int const dstW = config::previewWidth();
    int const dstH = config::previewHeight();

    paimon::ThreadTracker::get().spawn([levelID, rgba, width, height, dstW, dstH]() {
        geode::utils::thread::setName("PaimonAutoPreview");
        if (paimon::isRuntimeShuttingDown()) return;
        int targetW = dstW;
        int targetH = dstH;
        if (width > 0 && height > 0) {
            float srcAspect = static_cast<float>(width) / static_cast<float>(height);
            float dstAspect = static_cast<float>(dstW) / static_cast<float>(dstH);
            if (srcAspect > dstAspect) {
                targetH = std::max(1, static_cast<int>(dstW / srcAspect));
                targetW = dstW;
            } else {
                targetW = std::max(1, static_cast<int>(dstH * srcAspect));
                targetH = dstH;
            }
        }

        auto scaled = downscaleRGBA(rgba.get(), width, height, targetW, targetH);
        if (!scaled) return;
        if (paimon::isRuntimeShuttingDown()) return;

        bool ok = AutoPreviewStore::get().save(levelID, scaled.get(),
                                               static_cast<uint32_t>(targetW),
                                               static_cast<uint32_t>(targetH));
        if (!ok) return;
        AutoPreviewStore::get().noteGenerated();

        geode::Loader::get()->queueInMainThread([levelID]() {
            if (paimon::isRuntimeShuttingDown()) return;
            ThumbnailLoader::get().invalidateLevel(levelID);
        });
    });
}

} // namespace paimon::autopreview
