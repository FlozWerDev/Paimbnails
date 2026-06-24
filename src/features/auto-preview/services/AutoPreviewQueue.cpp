#include "AutoPreviewQueue.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <algorithm>
#include <vector>

#include "AutoPreviewStore.hpp"
#include "OffscreenLevelRenderer.hpp"
#include "LevelDataProvider.hpp"
#include "../AutoPreviewConfig.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::autopreview {

namespace {
constexpr float kTickSeconds = 0.5f;
constexpr auto kStaleAfter = std::chrono::milliseconds(6000); // dropped if not re-seen
} // namespace

AutoPreviewQueue& AutoPreviewQueue::get() {
    static auto* inst = new AutoPreviewQueue();
    return *inst;
}

void AutoPreviewQueue::enqueueIfEligible(GJGameLevel* level) {
    if (!config::browserGenEnabled()) return;
    if (!level) return;
    int const id = level->m_levelID.value();
    if (id <= 0) return;

    auto& store = AutoPreviewStore::get();
    if (store.has(id) || store.wasAttempted(id)) return;
    if (config::maxPerSession() <= 0) return;
    if (store.generatedThisSession() >= config::maxPerSession()) return;

    auto const now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_pending.find(id);
        if (it == m_pending.end()) {
            m_pending.emplace(id, Pending{ Ref<GJGameLevel>(level), now, now });
        } else {
            it->second.lastSeen = now;
        }
    }
    ensureTickScheduled();
}

void AutoPreviewQueue::ensureTickScheduled() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tickScheduled) return;
        m_tickScheduled = true;
    }
    paimon::scheduleMainThreadDelay(kTickSeconds, []() {
        AutoPreviewQueue::get().tick();
    });
}

void AutoPreviewQueue::tick() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tickScheduled = false;
    }
    if (paimon::isRuntimeShuttingDown()) return;

    processNext();

    bool morePending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        morePending = !m_pending.empty();
    }
    if (morePending) ensureTickScheduled();
}

void AutoPreviewQueue::processNext() {
    if (m_active) return;
    if (!config::browserGenEnabled()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
        return;
    }
    if (PlayLayer::get() != nullptr) return;
    if (LevelEditorLayer::get() != nullptr) return;
    if (m_lastGenAt.time_since_epoch().count() != 0) {
        auto since = std::chrono::steady_clock::now() - m_lastGenAt;
        if (since < std::chrono::milliseconds(2500)) return; // retry next tick
    }

    auto& store = AutoPreviewStore::get();
    if (store.generatedThisSession() >= config::maxPerSession()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
        return;
    }

    int chosenId = 0;
    Ref<GJGameLevel> chosenLevel;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto const now = std::chrono::steady_clock::now();
        auto const dwell = std::chrono::milliseconds(config::dwellMs());

        std::chrono::steady_clock::time_point oldestFirstSeen = now;
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (now - it->second.lastSeen > kStaleAfter) {
                it = m_pending.erase(it);
                continue;
            }
            if (now - it->second.firstSeen >= dwell) {
                if (it->second.firstSeen <= oldestFirstSeen) {
                    oldestFirstSeen = it->second.firstSeen;
                    chosenId = it->first;
                    chosenLevel = it->second.level;
                }
            }
            ++it;
        }
        if (chosenId != 0) {
            m_pending.erase(chosenId);
        }
    }

    if (chosenId == 0 || !chosenLevel) return;

    auto* level = chosenLevel.data();
    if (!level) return;
    store.markAttempted(chosenId);
    m_active = true;
    m_lastGenAt = std::chrono::steady_clock::now();
    if (!level->m_levelString.empty()) {
        generateFor(level, chosenId);
        m_active = false;
        return;
    }
    Ref<GJGameLevel> keep = chosenLevel;
    LevelDataProvider::get().request(chosenId, [this, chosenId, keep](GJGameLevel* dl) {
        if (!paimon::isRuntimeShuttingDown()) {
            GJGameLevel* lvl = (dl && !dl->m_levelString.empty()) ? dl : nullptr;
            if (!lvl && keep && !keep->m_levelString.empty()) lvl = keep.data();
            if (lvl) generateFor(lvl, chosenId);
        }
        m_active = false;
        ensureTickScheduled();
    });
}

void AutoPreviewQueue::generateFor(GJGameLevel* level, int levelID) {
    if (!level || levelID <= 0 || level->m_levelString.empty()) return;
    long const objCount = static_cast<long>(level->m_objectCount.value());
    if (objCount > 0 && objCount > static_cast<long>(config::maxObjects())) {
        log::debug("[AutoPreview] skip level {} ({} objects > cap {})",
                   levelID, objCount, config::maxObjects());
        return;
    }

    int const w = config::previewWidth();
    int const h = config::previewHeight();
    auto rendered = OffscreenLevelRenderer::render(level, w, h);

    if (rendered.success && !rendered.rgba.empty() && rendered.width > 0 && rendered.height > 0) {
        auto& store = AutoPreviewStore::get();
        if (store.save(levelID, rendered.rgba.data(),
                       static_cast<uint32_t>(rendered.width),
                       static_cast<uint32_t>(rendered.height))) {
            store.noteGenerated();
            ThumbnailLoader::get().invalidateLevel(levelID);
            log::info("[AutoPreview] Tier 1 generated browser preview for level {}", levelID);
        }
    } else {
        log::debug("[AutoPreview] Tier 1 offscreen render produced no usable frame for level {}", levelID);
    }
}

} // namespace paimon::autopreview
