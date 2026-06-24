// LoadingLayer hook: starts the core asset preload (22 main-level thumbnails +
// emote catalog) and shows an X/Y progress label. Downloads run in the
// background and don't block the LoadingLayer; they continue if the user
// reaches the menu first.

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

// Core-set preload lives in core/PreloadActions.cpp
// (paimon::preload::startFullPreload), shared with MenuLayerPreloadFallback.cpp.

} // namespace

class $modify(PaimonLoadingLayer, LoadingLayer) {
    struct Fields {
        cocos2d::CCLabelBMFont* progressLabel = nullptr;
        bool updateScheduled = false;
        bool setupDone = false;
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

            this->createProgressLabel();
            this->updateProgressLabel(0.f);
            this->scheduleProgressUpdates();
        }

        LoadingLayer::loadAssets();
    }

    void createProgressLabel() {
        if (m_fields->progressLabel) return;

        auto label = cocos2d::CCLabelBMFont::create("Paimbnails: 0/0", "chatFont.fnt");
        label->setScale(0.55f);
        label->setOpacity(200);
        label->setID("paimbnails-preload-progress"_spr);

        auto winSize = cocos2d::CCDirector::get()->getWinSize();
        label->setPosition({winSize.width / 2.f, 28.f});
        this->addChild(label, 100);
        m_fields->progressLabel = label;
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
        if (!m_fields->progressLabel) return;

        using namespace paimon::preload;

        int loaded = getTotalLoaded();
        int total = getTotalCount();

        std::string text;
        if (total == 0) {
            text = "Paimbnails: cargando...";
        } else if (loaded < total) {
            text = fmt::format("Paimbnails: {}/{}", loaded, total);
        } else {
            text = fmt::format("Paimbnails: {}/{} listo!", loaded, total);
            this->unschedule(schedule_selector(PaimonLoadingLayer::updateProgressLabel));
            m_fields->updateScheduled = false;
        }
        m_fields->progressLabel->setString(text.c_str());
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLoadingLayer::updateProgressLabel));
        m_fields->updateScheduled = false;
        if (m_fields->progressLabel) {
            m_fields->progressLabel = nullptr;
        }
        LoadingLayer::onExit();
    }
};
