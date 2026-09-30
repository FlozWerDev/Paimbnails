#pragma once
#include <Geode/Geode.hpp>

class PaimonInfoTarget : public cocos2d::CCNode {
public:
    void onInfo(cocos2d::CCObject* sender);
    static PaimonInfoTarget* create();
    static PaimonInfoTarget* shared();
};

namespace PaimonInfo {

    // info button for popups.
    inline CCMenuItemSpriteExtra* createInfoBtn(
        std::string const& title,
        std::string const& desc,
        cocos2d::CCNode* /*parent*/,
        float scale = 0.56f
    ) {
        using namespace cocos2d;

        auto spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        if (!spr) return nullptr;
        spr->setScale(scale);

        auto* target = PaimonInfoTarget::shared();

        auto btn = CCMenuItemSpriteExtra::create(
            spr, target,
            menu_selector(PaimonInfoTarget::onInfo)
        );
        if (!btn) return nullptr;

        auto data = CCString::createWithFormat("%s\n---\n%s", title.c_str(), desc.c_str());
        btn->setUserObject(data);

        return btn;
    }

} // namespace paimoninfo




