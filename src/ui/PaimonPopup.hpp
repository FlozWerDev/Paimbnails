#pragma once

#include <Geode/ui/Popup.hpp>

class PaimonPopup : public geode::Popup {
public:
    void show() override;

protected:
    bool init(float width, float height, char const* bg = "GJ_square01.png",
        cocos2d::CCRect bgRect = {});
    bool init(cocos2d::CCSize size, char const* bg = "GJ_square01.png",
        cocos2d::CCRect bgRect = {});
    void setTitle(geode::ZStringView title, char const* font = "bigFont.fnt",
        float scale = 0.52f, float offset = 20.f);
};
