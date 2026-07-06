#pragma once
#include <Geode/Geode.hpp>
#include "EmotePickerPopup.hpp"

namespace paimon::emotes {

struct EmoteInputContext {
    geode::CopyableFunction<std::string()> getText;
    geode::CopyableFunction<void(std::string const&)> setText;
    int charLimit = 140;
    EmotePickerPopup::LayoutSize pickerSize = EmotePickerPopup::LayoutSize::Normal;
    bool centerPicker = false;
};

class EmoteButton : public CCMenuItemSpriteExtra {
    EmoteInputContext m_context;
    geode::Ref<EmotePickerPopup> m_activePicker = nullptr;

    bool init(EmoteInputContext context);
    void onToggle(cocos2d::CCObject*);
    void loadRandomEmote();

    void onEnter() override {
        CCMenuItemSpriteExtra::onEnter();
        m_baseScale = getScale();
    }

public:
    static EmoteButton* create(EmoteInputContext context);
};

} // namespace paimon::emotes
