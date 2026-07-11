#pragma once
// Own-profile stats must come from GameStatsManager (local, live), not from the
// cached GJUserScore returned by RobTop. The server score lags until the next
// successful updateUserScore round-trip, and Paimbnails' userinfo disk cache can
// freeze that lag for days. Profile redesign already did this for its strip;
// vanilla ProfilePage must too so toggling redesign off doesn't show stale numbers.

#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GameToolbox.hpp>
#include <string>

namespace paimon::profiles {

// GSM keys used by GD for the main profile stats strip.
inline void applyLiveOwnProfileStats(GJUserScore* score) {
    if (!score) return;
    auto* gsm = GameStatsManager::sharedState();
    if (!gsm) return;
    score->m_stars = gsm->getStat("6");
    score->m_moons = gsm->getStat("28");
    score->m_diamonds = gsm->getStat("13");
    score->m_demons = gsm->getStat("5");
    score->m_userCoins = gsm->getStat("12");
    score->m_secretCoins = gsm->getStat("8");
}

// After loadPageFromUserInfo has built node-ids labels, force the visible
// numbers to match the (already patched) score. Safe no-op if menus/labels
// are missing (redesign hides stats-menu; other mods may rebuild it).
inline void refreshVanillaStatsLabels(cocos2d::CCNode* mainLayer, GJUserScore* score) {
    if (!mainLayer || !score) return;

    auto setLabel = [&](char const* id, int value) {
        // node-ids: "{}-label" inside "{}-label-container" under stats-menu
        auto* labelNode = mainLayer->getChildByIDRecursive(fmt::format("{}-label", id));
        auto* lbl = typeinfo_cast<cocos2d::CCLabelBMFont*>(labelNode);
        if (!lbl) return;
        auto const text = std::string(GameToolbox::pointsToString(value));
        if (std::string(lbl->getString()) == text) return;
        lbl->setString(text.c_str());
        if (auto* container = mainLayer->getChildByIDRecursive(fmt::format("{}-label-container", id))) {
            container->setContentSize({
                lbl->getScaledContentSize().width,
                lbl->getScaledContentSize().height
            });
            lbl->setPosition({0.f, container->getContentSize().height / 2.f});
        }
    };

    setLabel("stars", score->m_stars);
    setLabel("moons", score->m_moons);
    setLabel("diamonds", score->m_diamonds);
    setLabel("demons", score->m_demons);
    setLabel("user-coins", score->m_userCoins);
    setLabel("coins", score->m_secretCoins);
    if (score->m_creatorPoints > 0) {
        setLabel("creator-points", score->m_creatorPoints);
    }

    if (auto* statsMenu = typeinfo_cast<cocos2d::CCMenu*>(
            mainLayer->getChildByIDRecursive("stats-menu"))) {
        statsMenu->updateLayout();
    }
}

} // namespace paimon::profiles
