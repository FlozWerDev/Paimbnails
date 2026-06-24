#include "AutoPreviewGenerator.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

#include "AutoPreviewStore.hpp"
#include "../AutoPreviewConfig.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"

using namespace geode::prelude;

namespace paimon::autopreview {

std::unique_ptr<uint8_t[]> downscaleRGBA(
    uint8_t const* src, int srcW, int srcH, int dstW, int dstH) {
    if (!src || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return nullptr;

    auto out = std::make_unique<uint8_t[]>(static_cast<size_t>(dstW) * dstH * 4);

    if (dstW >= srcW && dstH >= srcH) {
        if (dstW == srcW && dstH == srcH) {
            std::memcpy(out.get(), src, static_cast<size_t>(srcW) * srcH * 4);
            return out;
        }
    }
    // pixels in its footprint. Good enough quality for tiny thumbnails, cheap.
    for (int dy = 0; dy < dstH; ++dy) {
        int sy0 = static_cast<int>(static_cast<int64_t>(dy) * srcH / dstH);
        int sy1 = static_cast<int>(static_cast<int64_t>(dy + 1) * srcH / dstH);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        sy1 = std::min(sy1, srcH);
        for (int dx = 0; dx < dstW; ++dx) {
            int sx0 = static_cast<int>(static_cast<int64_t>(dx) * srcW / dstW);
            int sx1 = static_cast<int>(static_cast<int64_t>(dx + 1) * srcW / dstW);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            sx1 = std::min(sx1, srcW);

            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                uint8_t const* row = src + (static_cast<size_t>(sy) * srcW + sx0) * 4;
                for (int sx = sx0; sx < sx1; ++sx) {
                    r += row[0]; g += row[1]; b += row[2]; a += row[3];
                    row += 4;
                    ++n;
                }
            }
            if (n == 0) n = 1;
            uint8_t* o = out.get() + (static_cast<size_t>(dy) * dstW + dx) * 4;
            o[0] = static_cast<uint8_t>(r / n);
            o[1] = static_cast<uint8_t>(g / n);
            o[2] = static_cast<uint8_t>(b / n);
            o[3] = static_cast<uint8_t>(a / n);
        }
    }
    return out;
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
