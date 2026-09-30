#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <functional>

namespace paimon::foryou {

class LevelTagsGatePopup : public geode::Popup {
public:
    // `oncontinue` runs when the user chooses to browse without tags.
    static LevelTagsGatePopup* create(std::function<void()> onContinue);

    static constexpr char const* kLevelTagsModID = "kampwski.level_tags";

    // opens the mod's page in geode's mods list. geode shows its own error
    // popup when the servers don't know the id.
    static void openModPage();

protected:
    bool init(std::function<void()> onContinue);

    void onInstall(cocos2d::CCObject* sender);
    void onContinueWithout(cocos2d::CCObject* sender);

    std::function<void()> m_onContinue;
};

} // namespace paimon::foryou
