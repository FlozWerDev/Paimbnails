#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <string>

// Fullscreen viewer for mod preview images with prev/next navigation.
// Strip idea compatible with "Mod Previews" by Alphalaneous; layout and
// navigation below are our own (see THIRD-PARTY-NOTICES.md).

namespace paimon::mod_previews {

class ModPreviewGalleryPopup : public geode::Popup {
public:
    // index: initial image (1-based). total: image count. base: URL prefix
    // that becomes the full URL as base + "<n>.png".
    static ModPreviewGalleryPopup* create(int index, int total, std::string base);

protected:
    int m_index = 1;
    int m_total = 1;
    int m_gen = 0; // bumped per navigation; stale load callbacks drop out
    std::string m_base;
    geode::LazySprite* m_sprite = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    ::CCMenuItemSpriteExtra* m_backBtn = nullptr;
    ::CCMenuItemSpriteExtra* m_fwdBtn = nullptr;

    bool init(int index, int total, std::string base);
    void openIndex(int index);
    void refreshChrome();
    void onBack(cocos2d::CCObject*);
    void onFwd(cocos2d::CCObject*);
};

} // namespace paimon::mod_previews
