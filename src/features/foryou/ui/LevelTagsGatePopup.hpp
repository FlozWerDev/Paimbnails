#pragma once

// Shown when the For You feed wants Level Tags and it isn't installed.
//
// The tags are what let the feed reason about *what a level is* rather than
// just how hard it is, so the popup explains that and then hands the user
// straight to the mod's page in Geode's own mods list, where they can install
// it in one click.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <functional>

namespace paimon::foryou {

class LevelTagsGatePopup : public geode::Popup {
public:
    // `onContinue` runs when the user chooses to browse without tags.
    static LevelTagsGatePopup* create(std::function<void()> onContinue);

    // The mod ID Level Tags publishes under.
    static constexpr char const* kLevelTagsModID = "kampwski.level_tags";

    // Opens the mod's page in Geode's mods list. Geode shows its own error
    // popup when the servers don't know the ID.
    static void openModPage();

protected:
    bool init(std::function<void()> onContinue);

    void onInstall(cocos2d::CCObject* sender);
    void onContinueWithout(cocos2d::CCObject* sender);

    std::function<void()> m_onContinue;
};

} // namespace paimon::foryou
