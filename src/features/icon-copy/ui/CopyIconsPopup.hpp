#pragma once
// Profile side of the icon clipboard: shows the set you are about to take and
// splits the two things you may want into their own buttons. Every icon in the
// strip opens its own card telling you where that icon comes from.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../IconCopyStore.hpp"

namespace paimon::iconcopy {

class CopyIconsPopup : public geode::Popup {
public:
    // `saved` is the garage view of a set you already copied: same card, but the
    // two copy actions make no sense there.
    static CopyIconsPopup* create(IconSet const& set, bool saved = false);

protected:
    bool init(IconSet const& set, bool saved);

    void onCopyAndUse(cocos2d::CCObject*);
    void onCopy(cocos2d::CCObject*);
    void onUse(cocos2d::CCObject*);

    void buildStrip(float centreX, float centreY);
    void openDetail(IconType type, cocos2d::CCNode* slot);

    IconSet m_set;
};

}  // namespace paimon::iconcopy
