#include "CompactListRefresh.hpp"

#include <Geode/Geode.hpp>
#include "../../../utils/MainThreadDelay.hpp"

using namespace geode::prelude;

namespace paimon::thumbnails {
    namespace {
        LevelBrowserLayer* findLevelBrowserInTree(CCNode* node) {
            if (!node) {
                return nullptr;
            }

            if (auto* browser = typeinfo_cast<LevelBrowserLayer*>(node)) {
                return browser;
            }

            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (auto* browser = findLevelBrowserInTree(child)) {
                    return browser;
                }
            }

            return nullptr;
        }

        LevelBrowserLayer* findActiveLevelBrowserLayer() {
            auto* scene = CCDirector::get()->getRunningScene();
            return findLevelBrowserInTree(scene);
        }

        void rebuildBrowserList(LevelBrowserLayer* browser) {
            if (!browser || !browser->m_searchObject) {
                return;
            }

            Ref<GJSearchObject> search = browser->m_searchObject;
            CCDirector::get()->replaceScene(LevelBrowserLayer::scene(search));
        }
    }

    void refreshLevelBrowserForCompactToggle(LevelBrowserLayer* browser) {
        rebuildBrowserList(browser);
    }

    void refreshActiveLevelBrowserForCompactToggle() {
        paimon::scheduleMainThreadDelay(0.f, []() {
            rebuildBrowserList(findActiveLevelBrowserLayer());
        });
    }
}
