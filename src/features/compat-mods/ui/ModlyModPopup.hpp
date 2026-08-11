#pragma once

#include "../services/ModlyTypes.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

namespace paimon::compat_mods {

// Full card for one Modly project: logo, badges, description, previews, the
// author shortcut, the social links and the comments shortcut.
class ModlyModPopup : public geode::Popup {
public:
    static ModlyModPopup* create(ModlyMod const& mod);

protected:
    ModlyMod m_mod;

    bool init(ModlyMod const& mod);
    void buildHeader();
    void buildBody();
    void buildButtons();
    cocos2d::CCNode* buildPreviewStrip(float width);

    void onAuthor(cocos2d::CCObject*);
    void onComments(cocos2d::CCObject*);
    void onDownload(cocos2d::CCObject*);
    void onRepo(cocos2d::CCObject*);
    void onDiscord(cocos2d::CCObject*);
    void onKofi(cocos2d::CCObject*);
    void onPreview(cocos2d::CCObject* sender);
};

} // namespace paimon::compat_mods
