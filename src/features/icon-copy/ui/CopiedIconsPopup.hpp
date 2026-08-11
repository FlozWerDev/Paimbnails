#pragma once
// Garage side of the icon clipboard: every set you copied, with the name of the
// user it came from.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <cstddef>

namespace paimon::iconcopy {

struct IconSet;

class CopiedIconsPopup : public geode::Popup {
public:
    static CopiedIconsPopup* create();

protected:
    bool init();
    void rebuild();
    cocos2d::CCNode* makeRow(IconSet const& set, size_t index, float width);

    void use(IconSet const& set);
    void showIcons(IconSet const& set);
    void erase(IconSet const& set);
    void onClearAll(cocos2d::CCObject*);
    void queueRebuild();

    cocos2d::CCNode* m_body = nullptr;
    CCMenuItemSpriteExtra* m_clearBtn = nullptr;
};

}  // namespace paimon::iconcopy
