#pragma once

// Tags let the feed reason about what a level is, not just how hard it is.
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
