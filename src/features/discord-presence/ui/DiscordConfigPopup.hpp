#pragma once

#include <Geode/Geode.hpp>

namespace paimon::discord {

class DiscordConfigPopup : public geode::Popup {
public:
    static DiscordConfigPopup* create();

protected:
    bool init() override;
    void onExit() override;

    void onOpenGeodeSettings(cocos2d::CCObject*);
    void onResetDefaults(cocos2d::CCObject*);
    void onRefreshPresence(cocos2d::CCObject*);

    geode::TextInput* m_detailsInput = nullptr;
    geode::TextInput* m_stateInput = nullptr;
    geode::TextInput* m_largeTextInput = nullptr;
    geode::TextInput* m_largeImageKeyInput = nullptr;
    geode::TextInput* m_smallImageKeyInput = nullptr;
};

} // namespace paimon::discord
