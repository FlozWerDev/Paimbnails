#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/Layout.hpp>
#include <string>

class PaimonPopup : public geode::Popup {
public:
    void show() override;

protected:
    bool init(float width, float height, char const* bg = "GJ_square01.png",
        cocos2d::CCRect bgRect = {});
    bool init(cocos2d::CCSize size, char const* bg = "GJ_square01.png",
        cocos2d::CCRect bgRect = {});
    void setTitle(geode::ZStringView title, char const* font = "goldFont.fnt",
        float scale = 0.7f, float offset = 20.f);

    // GJ_infoIcon in the top-right corner by default; body accepts GD color tags.
    CCMenuItemSpriteExtra* addInfoButton(std::string const& title, std::string const& body,
        geode::Anchor anchor = geode::Anchor::TopRight, cocos2d::CCPoint offset = {-18.f, -18.f});
    void addCorners(geode::SideArtStyle style = geode::SideArtStyle::PopupGold,
        float scale = 0.45f, bool top = false);
};
