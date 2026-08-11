// Fallback that starts the preload and shows the X/Y label even if the
// LoadingLayer hook never intercepted anything (e.g. an early-load:false mod
// that finished loading after LoadingLayer::loadAssets). Only claims the
// preload/label if LoadingLayer didn't (tryClaimPreload), so it isn't duplicated.

#include <Geode/modify/MenuLayer.hpp>

#include <fmt/format.h>

#include "../core/PreloadProgress.hpp"
#include "../core/PreloadActions.hpp"
#include "../core/MainLevels.hpp"
#include "../core/MainLevelPrefetch.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../utils/HttpClient.hpp"
#include "../framework/HookConventions.hpp"

using namespace geode::prelude;

namespace {

constexpr float kProgressUpdateInterval = 0.1f;

// Core-set preload (main-level thumbnails + emotes) lives in
// core/PreloadActions.cpp (paimon::preload::startFullPreload), shared with LoadingLayer.cpp.

} // namespace

class $modify(PaimonMenuLayerPreload, MenuLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "MenuLayer::init");
    }

    struct Fields {
        cocos2d::CCLabelBMFont* progressLabel = nullptr;
        bool updateScheduled = false;
        // True if this instance started the preload. If false (LoadingLayer
        // already started it), still show the label unless it finished.
        bool ownsPreload = false;
    };

    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }


        // If nobody claimed it yet (LoadingLayer hook never ran), start it here;
        // otherwise just observe the counters.
        if (paimon::preload::tryClaimPreload()) {
            m_fields->ownsPreload = true;
            paimon::preload::startFullPreload();
        }

        // Show the label only if the preload started (total > 0) and hasn't finished.
        if (paimon::preload::getTotalCount() > 0 && !paimon::preload::isFinished()) {
            this->createPreloadLabel();
            this->updatePreloadLabel(0.f);
            this->schedulePreloadLabelUpdates();
        } else if (m_fields->ownsPreload) {
            // Edge case: we started the preload but the emote catalog hasn't
            // arrived (total=0); show the label until the total is known.
            this->createPreloadLabel();
            this->updatePreloadLabel(0.f);
            this->schedulePreloadLabelUpdates();
        }

        return true;
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonMenuLayerPreload::updatePreloadLabel));
        this->unschedule(schedule_selector(PaimonMenuLayerPreload::removePreloadLabel));
        if (m_fields->progressLabel) {
            m_fields->progressLabel = nullptr;
        }
        m_fields->updateScheduled = false;
        MenuLayer::onExit();
    }

    void createPreloadLabel() {
        if (m_fields->progressLabel) return;
        // Also bail if another instance already added it (unlikely, defensive).
        if (this->getChildByID("paimbnails-menu-preload-progress"_spr)) return;

        auto label = cocos2d::CCLabelBMFont::create("Paimbnails: 0/0", "chatFont.fnt");
        label->setScale(0.45f);
        label->setOpacity(180);
        label->setID("paimbnails-menu-preload-progress"_spr);
        label->setAnchorPoint({0.f, 1.f});

        auto winSize = cocos2d::CCDirector::get()->getWinSize();
        // Top-left corner, clear of the username/control buttons.
        label->setPosition({6.f, winSize.height - 6.f});
        this->addChild(label, 1000);
        m_fields->progressLabel = label;
    }

    void schedulePreloadLabelUpdates() {
        if (m_fields->updateScheduled) return;
        m_fields->updateScheduled = true;
        this->schedule(
            schedule_selector(PaimonMenuLayerPreload::updatePreloadLabel),
            kProgressUpdateInterval
        );
    }

    void updatePreloadLabel(float /*dt*/) {
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
            this->unschedule(schedule_selector(PaimonMenuLayerPreload::updatePreloadLabel));
            m_fields->updateScheduled = false;
            // Remove the label 2s after it reaches the done state so it doesn't linger.
            auto label = m_fields->progressLabel;
            this->scheduleOnce(
                schedule_selector(PaimonMenuLayerPreload::removePreloadLabel),
                2.0f
            );
            (void)label;
        }
        m_fields->progressLabel->setString(text.c_str());
    }

    void removePreloadLabel(float /*dt*/) {
        if (m_fields->progressLabel) {
            m_fields->progressLabel->removeFromParent();
            m_fields->progressLabel = nullptr;
        }
    }
};
