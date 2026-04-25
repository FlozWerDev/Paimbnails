#include <Geode/Geode.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include "../features/community/ui/LeaderboardLayer.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/compat/SceneLocators.hpp"

using namespace geode::prelude;

class $modify(MyLevelSearchLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("LevelSearchLayer::init", "geode.node-ids");
    }

    $override
    bool init(int searchType) {
        if (!LevelSearchLayer::init(searchType)) return false;

        // fondo custom
        bool hasCustomBg = LayerBackgroundManager::get().applyBackground(this, "search");

        // si tenemos fondo, ocultar los sprites que GD pone de decoracion
        if (hasCustomBg) {
            static char const* hideIDs[] = {
                "level-search-bg",
                "quick-search-bg",
                "difficulty-filters-bg",
                "length-filters-bg",
                nullptr
            };
            for (int i = 0; hideIDs[i]; i++) {
                if (auto node = this->getChildByID(hideIDs[i])) {
                    node->setVisible(false);
                }
            }
        }

        CCSprite* spr = CCSprite::createWithSpriteFrameName("GJ_starBtn_001.png");
        if (!paimon::SpriteHelper::isValidSprite(spr)) {
            spr = CCSprite::create("paim_Daily.png"_spr);
            if (!paimon::SpriteHelper::isValidSprite(spr)) {
                spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
            }
        }
        if (!paimon::SpriteHelper::isValidSprite(spr)) {
            log::warn("Could not create leaderboard button sprite in LevelSearchLayer");
            return true;
        }

        float targetSize = 35.0f;
        float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
        if (currentSize > 0) {
            spr->setScale(targetSize / currentSize);
        }

        auto btn = CCMenuItemSpriteExtra::create(
            spr,
            this,
            menu_selector(MyLevelSearchLayer::onLeaderboard)
        );
        btn->setID("paimon-leaderboard-btn"_spr);

        if (auto menu = this->getChildByID("other-filter-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            if (auto fallbackMenu = paimon::compat::LevelBrowserLocator::findSearchMenu(this)) {
                fallbackMenu->addChild(btn);
                fallbackMenu->updateLayout();
                log::warn("Using fallback menu locator in LevelSearchLayer");
            } else {
                log::warn("Could not find 'other-filter-menu' nor fallback menu in LevelSearchLayer");
            }
        }

        return true;
    }

    void onLeaderboard(CCObject*) {
        TransitionManager::get().replaceScene(LeaderboardLayer::scene(LeaderboardLayer::BackTarget::LevelSearchLayer));
    }
};
