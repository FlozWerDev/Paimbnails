#pragma once

// DiscordConfigPopup — popup para personalizar la Rich Presence de Paimbnails.
// Configuraciones disponibles: enable, private mode, idle, timestamps,
// activity type, custom details/state, tooltip del banner y asset keys.
// Los botones siempre apuntan al GitHub y Discord del mod (fijo).

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

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

    // Status inputs
    geode::TextInput* m_detailsInput = nullptr;
    geode::TextInput* m_stateInput = nullptr;
    geode::TextInput* m_largeTextInput = nullptr;
    geode::TextInput* m_largeImageKeyInput = nullptr;
    geode::TextInput* m_smallImageKeyInput = nullptr;
};

} // namespace paimon::discord
