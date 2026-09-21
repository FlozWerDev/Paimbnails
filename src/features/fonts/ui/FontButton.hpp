#pragma once

#include <Geode/Geode.hpp>
#include "FontPickerPopup.hpp"

namespace paimon::fonts {

class FontButton : public CCMenuItemSpriteExtra {
    geode::CopyableFunction<void(std::string const&)> m_insertFn;
    geode::Ref<FontPickerPopup> m_activePicker = nullptr;

    bool init(geode::CopyableFunction<void(std::string const&)> insertFn);
    void onToggle(cocos2d::CCObject*);

public:
    static FontButton* create(geode::CopyableFunction<void(std::string const&)> insertFn);
};

} // namespace paimon::fonts
