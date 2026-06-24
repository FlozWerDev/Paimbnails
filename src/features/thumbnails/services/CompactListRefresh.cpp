#include "CompactListRefresh.hpp"

#include <Geode/Geode.hpp>
#include "../../../utils/MainThreadDelay.hpp"

using namespace geode::prelude;

namespace paimon::thumbnails {
    namespace {
        LevelBrowserLayer* findActiveLevelBrowserLayer() {
            auto* scene = CCDirector::get()->getRunningScene();
            if (!scene) {
                return nullptr;
            }

            if (auto* browser = scene->getChildByType<LevelBrowserLayer>(0)) {
                return browser;
            }

            for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
                if (auto* browser = typeinfo_cast<LevelBrowserLayer*>(child)) {
                    return browser;
                }
            }

            return nullptr;
        }

        void rebuildBrowserList(LevelBrowserLayer* browser) {
            if (!browser || !browser->m_searchObject) {
                return;
            }

            // Remove the current list so GD creates fresh cells with the new
            // compact-mode type/height instead of trying to recycle stale ones.
            if (browser->m_list) {
                browser->m_list->removeFromParentAndCleanup(true);
                browser->m_list = nullptr;
            }

            if (browser->m_levels) {
                browser->setupLevelBrowser(browser->m_levels);
                browser->updatePageLabel();
                browser->updateLevelsLabel();
            } else {
                browser->loadPage(browser->m_searchObject);
            }
        }
    }

    void refreshActiveLevelBrowserForCompactToggle() {
        paimon::scheduleMainThreadDelay(0.f, []() {
            rebuildBrowserList(findActiveLevelBrowserLayer());
        });
    }
}
