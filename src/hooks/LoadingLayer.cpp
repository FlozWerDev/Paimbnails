// LoadingLayer hook: preloads the 22 main-level thumbnails before the menu.
// The emote catalog continues in the background afterwards.

#include <Geode/modify/LoadingLayer.hpp>

#include <fmt/format.h>

#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../framework/HookConventions.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../utils/HttpClient.hpp"
#include "../core/MainLevels.hpp"
#include "../core/MainLevelPrefetch.hpp"
#include "../core/PreloadProgress.hpp"
#include "../core/PreloadActions.hpp"
#include "../utils/MainThread.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace {

constexpr float kProgressUpdateInterval = 0.1f;
constexpr int kFinalGameLoadStep = 14;

// Core-set preload lives in core/PreloadActions.cpp
// (paimon::preload::startFullPreload), shared with MenuLayerPreloadFallback.cpp.

} // namespace

class $modify(PaimonLoadingLayer, LoadingLayer) {
    struct Fields {
        bool updateScheduled = false;
        bool setupDone = false;
        bool waitingForMainLevels = false;
        bool mainLevelsFinished = false;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LoadingLayer::loadAssets");
    }

    bool init(bool fromReload) {
        if (!LoadingLayer::init(fromReload)) {
            return false;
        }
        paimon::captureMainThread();
        LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);
        return true;
    }

    void loadAssets() {
        if (!m_fields->setupDone) {
            m_fields->setupDone = true;

            if (paimon::preload::tryClaimPreload()) {
                paimon::preload::startFullPreload();
            }

            this->updateProgressLabel(0.f);
            this->scheduleProgressUpdates();
        }

        int loaded = paimon::preload::g_thumbsLoaded.load(std::memory_order_acquire);
        int total = paimon::preload::g_thumbsTotal.load(std::memory_order_acquire);
        if (m_loadStep == kFinalGameLoadStep && !m_fields->mainLevelsFinished) {
            if (total > 0 && loaded < total) {
                m_fields->waitingForMainLevels = true;
                return;
            }
            m_fields->mainLevelsFinished = true;
        }

        LoadingLayer::loadAssets();
    }

    void scheduleProgressUpdates() {
        if (m_fields->updateScheduled) return;
        m_fields->updateScheduled = true;
        this->schedule(
            schedule_selector(PaimonLoadingLayer::updateProgressLabel),
            kProgressUpdateInterval
        );
    }

    void updateProgressLabel(float /*dt*/) {
        auto label = static_cast<cocos2d::CCLabelBMFont*>(
            this->getChildByID("geode-small-label")
        );
        int loaded = paimon::preload::g_thumbsLoaded.load(std::memory_order_acquire);
        int total = paimon::preload::g_thumbsTotal.load(std::memory_order_acquire);

        std::string text;
        if (total == 0) {
            text = "Paimbnails: preparando miniaturas...";
        } else if (loaded < total) {
            text = fmt::format("Paimbnails: cargando miniaturas... ({}/{})", loaded, total);
        } else {
            text = fmt::format("Paimbnails: miniaturas listas! ({}/{})", loaded, total);
            this->unschedule(schedule_selector(PaimonLoadingLayer::updateProgressLabel));
            m_fields->updateScheduled = false;
        }
        if (label) label->setString(text.c_str());

        if (total > 0 && loaded >= total && m_fields->waitingForMainLevels) {
            m_fields->waitingForMainLevels = false;
            m_fields->mainLevelsFinished = true;
            LoadingLayer::loadAssets();
        }
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLoadingLayer::updateProgressLabel));
        m_fields->updateScheduled = false;
        LoadingLayer::onExit();
    }
};
