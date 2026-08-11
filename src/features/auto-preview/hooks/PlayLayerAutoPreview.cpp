#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <atomic>

#include "../AutoPreviewConfig.hpp"
#include "../services/AutoPreviewStore.hpp"
#include "../services/AutoPreviewGenerator.hpp"
#include "../../capture/services/FramebufferCapture.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../gameplay-performance/GameplayPerformance.hpp"
#include "../../../utils/ActivePauseLayer.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace {

std::atomic<bool> g_autoCaptureBusy{false};

bool levelHasNoThumbnail(int levelID) {
    if (levelID <= 0) return false;
    if (LocalThumbs::get().has(levelID)) return false;
    if (paimon::autopreview::AutoPreviewStore::get().has(levelID)) return false;
    return ThumbnailLoader::get().isNotFound(levelID);
}

void attemptAutoCapture(WeakRef<PlayLayer> weak, int levelID) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (paimon::gameplayperf::isOptionEnabled(
            paimon::gameplayperf::kAutoPreviewModuleId)) return;

    auto locked = weak.lock();
    if (!locked) return;
    auto* pl = static_cast<PlayLayer*>(locked.data());
    if (!pl || !pl->getParent()) return;
    if (PlayLayer::get() != pl) return;
    if (paimon::hasPauseLayerInScene()) return;

    if (!paimon::autopreview::config::enabled()) return;
    if (!levelHasNoThumbnail(levelID)) return;

    if (paimon::isCaptureInProgress()) return;
    if (FramebufferCapture::isCapturing() || FramebufferCapture::hasPendingCapture()) return;

    bool expected = false;
    if (!g_autoCaptureBusy.compare_exchange_strong(expected, true)) return;

    auto validation = FramebufferCapture::validateCaptureConditions();
    if (!validation.canCapture) {
        g_autoCaptureBusy.store(false);
        return;
    }

    paimon::setCaptureInProgress(true);

    FramebufferCapture::requestCapture(
        levelID,
        [levelID](bool success, CCTexture2D*, std::shared_ptr<uint8_t> rgbaData,
                  int width, int height) {
            Loader::get()->queueInMainThread([success, rgbaData, width, height, levelID]() {
                paimon::setCaptureInProgress(false);
                g_autoCaptureBusy.store(false);
                if (paimon::isRuntimeShuttingDown()) return;
                if (!success || !rgbaData || width <= 0 || height <= 0) return;
                paimon::autopreview::storeCapturedFrame(levelID, rgbaData, width, height);
            });
        },
        nullptr,
        true,
        true
    );
}

} // namespace

class $modify(PaimonAutoPreviewPlayLayer, PlayLayer) {
    struct Fields {
        bool m_autoPreviewScheduled = false;
    };

    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (paimon::gameplayperf::isOptionEnabled(
                paimon::gameplayperf::kAutoPreviewModuleId)) return true;
        if (!paimon::autopreview::config::enabled()) return true;
        if (!level) return true;
        int const levelID = level->m_levelID.value();
        if (levelID <= 0) return true;
        if (paimon::autopreview::AutoPreviewStore::get().wasAttempted(levelID)) return true;
        // Quick pre-check (re-checked again right before capture).
        if (!levelHasNoThumbnail(levelID)) return true;

        m_fields->m_autoPreviewScheduled = true;
        paimon::autopreview::AutoPreviewStore::get().markAttempted(levelID);
        float const delay = 1.2f + static_cast<float>(levelID % 1000) / 1000.0f * 1.8f;

        WeakRef<PlayLayer> weak = this;
        paimon::scheduleMainThreadDelay(delay, [weak, levelID]() {
            attemptAutoCapture(weak, levelID);
        });

        return true;
    }
};
