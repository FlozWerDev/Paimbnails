#pragma once

#include <Geode/Geode.hpp>

namespace paimon::discord {

class DiscordConfigPopup : public geode::Popup {
public:
    static DiscordConfigPopup* create();

protected:
    bool init() override;
    void onExit() override;
    void scrollWheel(float x, float y) override;

    void onOpenGeodeSettings(cocos2d::CCObject*);
    void onResetDefaults(cocos2d::CCObject*);
    void onRefreshPresence(cocos2d::CCObject*);

    void updatePreview();
    void updateSmoothScroll(float dt);

    geode::TextInput* m_detailsInput = nullptr;
    geode::TextInput* m_stateInput = nullptr;
    geode::TextInput* m_largeTextInput = nullptr;
    geode::TextInput* m_largeImageKeyInput = nullptr;
    geode::TextInput* m_smallImageKeyInput = nullptr;
    geode::TextInput* m_smallTextInput = nullptr;

    cocos2d::CCLabelBMFont* m_prevHeader = nullptr;
    cocos2d::CCLabelBMFont* m_prevDetails = nullptr;
    cocos2d::CCLabelBMFont* m_prevState = nullptr;
    cocos2d::CCLabelBMFont* m_prevTime = nullptr;
    cocos2d::CCLabelBMFont* m_prevSmall = nullptr;

    geode::ScrollLayer* m_scroll = nullptr;
    float m_scrollTargetY = 0.f;
    bool m_scrollTargetSet = false;
};

} // namespace paimon::discord
