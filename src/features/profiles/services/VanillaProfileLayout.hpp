#pragma once

#include "../../../core/Settings.hpp"
#include "../../../framework/compat/SceneLocators.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/ui/SimpleAxisLayout.hpp>
#include <algorithm>

namespace paimon::profiles {

inline cocos2d::CCSize vanillaPopupSize(cocos2d::CCNode* mainLayer, cocos2d::CCSize hint = {0.f, 0.f}) {
    if (hint.width > 100.f && hint.height > 100.f) return hint;
    if (mainLayer) {
        auto geo = paimon::compat::InfoLayerLocator::findPopupGeometry(mainLayer);
        if (geo.found && geo.size.width > 100.f && geo.size.height > 100.f) {
            return geo.size;
        }
    }
    return {440.f, 290.f};
}

inline void configureCompactAxisLayout(cocos2d::CCNode* menu, float gap, bool allowAutoScale = true) {
    using namespace geode::prelude;
    if (!menu) return;
    if (auto* axis = typeinfo_cast<AxisLayout*>(menu->getLayout())) {
        axis->ignoreInvisibleChildren(true);
        axis->setGrowCrossAxis(false);
        if (allowAutoScale) axis->setAutoScale(true);
        if (gap >= 0.f) axis->setGap(gap);
    } else if (auto* simple = typeinfo_cast<SimpleAxisLayout*>(menu->getLayout())) {
        simple->ignoreInvisibleChildren(true);
        if (gap >= 0.f) simple->setGap(gap);
    }
}

inline void normalizeVanillaLeftMenu(cocos2d::CCNode* mainLayer, cocos2d::CCSize popupSizeHint = {0.f, 0.f}) {
    using namespace geode::prelude;
    if (!mainLayer || paimon::settings::profiles::redesignEnabled()) return;
    auto* menu = typeinfo_cast<CCMenu*>(mainLayer->getChildByID("left-menu"));
    if (!menu) return;

    auto const popSize = vanillaPopupSize(mainLayer, popupSizeHint);
    float const maxHeight = std::max(140.f, popSize.height - 54.f);
    auto curSize = menu->getContentSize();
    if (curSize.height > maxHeight || curSize.height < 60.f) {
        menu->setContentSize({std::max(curSize.width, 32.f), std::clamp(curSize.height, 140.f, maxHeight)});
    }
    configureCompactAxisLayout(menu, 5.f, true);
    menu->updateLayout();
}

inline void normalizeVanillaSocialsMenu(cocos2d::CCNode* mainLayer, cocos2d::CCSize popupSizeHint = {0.f, 0.f}) {
    using namespace geode::prelude;
    if (!mainLayer || paimon::settings::profiles::redesignEnabled()) return;
    auto* menu = typeinfo_cast<CCMenu*>(mainLayer->getChildByID("socials-menu"));
    if (!menu) return;

    auto const popSize = vanillaPopupSize(mainLayer, popupSizeHint);
    float const maxHeight = std::max(120.f, popSize.height - 64.f);
    auto curSize = menu->getContentSize();
    if (curSize.height > maxHeight || curSize.height < 60.f) {
        menu->setContentSize({std::max(curSize.width, 32.f), std::clamp(curSize.height, 120.f, maxHeight)});
    }
    configureCompactAxisLayout(menu, 5.f, true);
    menu->updateLayout();
}

inline void normalizeVanillaUsernameMenu(cocos2d::CCNode* mainLayer, cocos2d::CCSize popupSizeHint = {0.f, 0.f}) {
    using namespace geode::prelude;
    if (!mainLayer || paimon::settings::profiles::redesignEnabled()) return;
    auto* menu = typeinfo_cast<CCMenu*>(mainLayer->getChildByIDRecursive("username-menu"));
    if (!menu) return;

    auto const popSize = vanillaPopupSize(mainLayer, popupSizeHint);
    // Keep clear of the top-left close button and right-side header chrome.
    float const maxWidth = std::clamp(popSize.width - 116.f, 200.f, 336.f);
    auto curSize = menu->getContentSize();
    menu->setContentSize({maxWidth, std::max(curSize.height, 26.f)});

    if (!menu->getLayout()) {
        menu->setLayout(
            RowLayout::create()
                ->setGap(4.f)
                ->setAxisAlignment(AxisAlignment::Center)
                ->setCrossAxisAlignment(AxisAlignment::Center)
                ->setAutoScale(true)
                ->setGrowCrossAxis(false)
                ->ignoreInvisibleChildren(true)
        );
    } else {
        configureCompactAxisLayout(menu, 4.f, true);
        if (auto* axis = typeinfo_cast<AxisLayout*>(menu->getLayout())) {
            axis->setAxisAlignment(AxisAlignment::Center);
            axis->setCrossAxisAlignment(AxisAlignment::Center);
        }
    }
    menu->updateLayout();
}

inline void normalizeVanillaStatsMenu(cocos2d::CCNode* mainLayer, cocos2d::CCSize popupSizeHint = {0.f, 0.f}) {
    using namespace geode::prelude;
    if (!mainLayer || paimon::settings::profiles::redesignEnabled()) return;
    auto* menu = typeinfo_cast<CCMenu*>(mainLayer->getChildByIDRecursive("stats-menu"));
    if (!menu) return;

    auto const popSize = vanillaPopupSize(mainLayer, popupSizeHint);
    float const maxWidth = std::clamp(popSize.width - 40.f, 260.f, 405.f);
    auto curSize = menu->getContentSize();
    if (curSize.width <= 0.f || curSize.width > maxWidth) {
        menu->setContentSize({maxWidth, std::max(curSize.height, 24.f)});
    }
    configureCompactAxisLayout(menu, -1.f, true);
    menu->updateLayout();
}

inline void normalizeVanillaBottomMenu(cocos2d::CCNode* mainLayer, cocos2d::CCSize popupSizeHint = {0.f, 0.f}) {
    using namespace geode::prelude;
    if (!mainLayer || paimon::settings::profiles::redesignEnabled()) return;
    auto* menu = typeinfo_cast<CCMenu*>(mainLayer->getChildByIDRecursive("bottom-menu"));
    if (!menu) return;

    configureCompactAxisLayout(menu, -1.f, true);
    menu->updateLayout();
}

} // namespace paimon::profiles
