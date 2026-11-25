#include "AutoPreviewGenerator.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <exception>

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
    // sanity: never queue absurd frames into the scaler (OOM risk on bad captures).
    constexpr int64_t kMaxPixels = static_cast<int64_t>(3840) * 2160 * 2;
    int64_t const pixels = static_cast<int64_t>(width) * height;
    if (pixels > kMaxPixels) {
        geode::log::debug("[AutoPreview] skip captured frame for level {} ({}x{} too large)",
                          levelID, width, height);
        return;
    }

    int const dstW = config::previewWidth();
    int const dstH = config::previewHeight();

    paimon::ThreadTracker::get().spawn([levelID, rgba, width, height, dstW, dstH]() {
        geode::utils::thread::setName("PaimonAutoPreview");
        try {
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
        } catch (std::exception const& ex) {
            geode::log::debug("[AutoPreview] captured frame for level {} dropped ({}x{} threw: {})",
                              levelID, width, height, ex.what());
        } catch (...) {
            geode::log::debug("[AutoPreview] captured frame for level {} dropped ({}x{} threw unknown)",
                              levelID, width, height);
        }
    });
}

} // namespace paimon::autopreview
