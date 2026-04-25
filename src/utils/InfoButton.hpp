#pragma once
#include <Geode/Geode.hpp>

// ────────────────────────────────────────────────────────────
// InfoButton: small "i" button that shows an FLAlertLayer
// with a description when clicked. Reusable across all popups.
// ────────────────────────────────────────────────────────────

// Helper node that receives the info button click callback.
// Implementation in InfoButton.cpp
class PaimonInfoTarget : public cocos2d::CCNode {
public:
    void onInfo(cocos2d::CCObject* sender);
    static PaimonInfoTarget* create();
    static PaimonInfoTarget* shared();
};

namespace PaimonInfo {

    // Creates a small "i" info button. Add to a CCMenu.
    // @param title   Alert popup title
    // @param desc    Multi-line description text
    // @param parent  Unused (kept for API compat)
    // @param scale   Icon scale (default 0.56)
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

        // encode title + desc into user object
        auto data = CCString::createWithFormat("%s\n---\n%s", title.c_str(), desc.c_str());
        btn->setUserObject(data);

        return btn;
    }

} // namespace PaimonInfo




