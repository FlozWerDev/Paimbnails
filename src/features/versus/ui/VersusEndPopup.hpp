#pragma once

#include "../data/VersusTypes.hpp"
#include "VersusRankBadgeNode.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

namespace paimon::versus {

// what a duel leaves behind: the result, the elo that moved, the tier if it
// changed, and the two ways out.
class VersusEndPopup : public geode::Popup {
public:
    static VersusEndPopup* create();

protected:
    bool init() override;

    void buildResult();
    void buildRankStrip();
    void buildButtons();

    void onRematch(cocos2d::CCObject* sender);
    void onReport(cocos2d::CCObject* sender);
    void onClose(cocos2d::CCObject* sender) override;

    VersusRankBadgeNode* m_badge = nullptr;
};

} // namespace paimon::versus
