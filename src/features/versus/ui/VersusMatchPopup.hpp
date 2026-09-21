#pragma once

#include "../data/VersusTypes.hpp"
#include "VersusRankBadgeNode.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <string>
#include <vector>

namespace paimon::versus {

class VersusMatchPopup : public geode::Popup {
public:
    static VersusMatchPopup* create();

protected:
    bool init() override;
    void onEnter() override;
    void onExit() override;

    void rebuild();
    uint32_t offerStamp() const;
    void buildSteps(cocos2d::CCNode* page, Phase phase);
    void buildFound(cocos2d::CCNode* page);
    void buildBanning(cocos2d::CCNode* page);
    void buildLoading(cocos2d::CCNode* page);

    void onAccept(cocos2d::CCObject* sender);
    void onDecline(cocos2d::CCObject* sender);
    void onBan(cocos2d::CCObject* sender);
    void onPlay(cocos2d::CCObject* sender);

    Phase m_drawn = Phase::Idle;
    uint32_t m_drawnOffers = 0;
    cocos2d::CCNode* m_page = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
};

} // namespace paimon::versus
