#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <vector>

class CCMenuItemToggler;

namespace paimon::scorecell {

class LeaderboardLayoutPopup : public geode::Popup {
public:
    static LeaderboardLayoutPopup* create();

    void setOnClose(geode::CopyableFunction<void()> callback) {
        m_onCloseCallback = std::move(callback);
    }

protected:
    bool initContents();
    void onClose(cocos2d::CCObject* sender) override;

private:
    void onPreset(cocos2d::CCObject* sender);
    void onModule(cocos2d::CCObject* sender);
    void onEffects(cocos2d::CCObject* sender);
    void refreshControls();

    geode::CopyableFunction<void()> m_onCloseCallback;
    geode::Ref<cocos2d::CCLabelBMFont> m_presetLabel;
    std::vector<geode::Ref<CCMenuItemToggler>> m_moduleToggles;
};

}
