#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../services/ModlyTypes.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

namespace paimon::compat_mods {

class ModlyModPopup : public PaimonPopup {
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
