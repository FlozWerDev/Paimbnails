#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <functional>
#include <vector>

namespace paimon::foryou {

// Quality tier used by the cycling rating button (bottom-left of the popup).
// Each tier is a superset of the previous one when seeded into the tracker.
enum class RatingTier : int {
    StarRated = 0, // rated levels
    Featured  = 1, // Featured + rated
    Epic      = 2, // Epic badge
    Legendary = 3, // Legendary badge
    Mythic    = 4, // Mythic badge
    Count     = 5
};

class ForYouPreferencesPopup : public geode::Popup {
public:
    static ForYouPreferencesPopup* create(std::function<void()> onConfirm);

protected:
    bool init(std::function<void()> onConfirm);

    // input callbacks
    void onDifficultySelect(cocos2d::CCObject* sender);
    void onDemonDiffSelect(cocos2d::CCObject* sender);
    void onGameModeSelect(cocos2d::CCObject* sender);
    void onLengthSelect(cocos2d::CCObject* sender);
    void onRatingCycle(cocos2d::CCObject* sender);
    void onConfirm(cocos2d::CCObject* sender);

    // visual refreshers
    void refreshDifficultyButtons();
    void refreshDemonButtons();
    void refreshGameModeButtons();
    void refreshLengthButtons();
    void refreshRatingTier();
    void refreshDemonRowVisibility();

    // ── state ──
    std::function<void()> m_onConfirm;
    int m_difficulty  = 30; // 10=Easy,20=Normal,30=Hard,40=Harder,50=Insane,60=Demon
    int m_demonDiff   = 0;  // 0=Any,1=Easy,2=Medium,3=Hard,4=Insane,5=Extreme
    int m_gameMode    = 0;  // 0=Classic,1=Platformer,2=Both
    int m_length      = 2;  // 0=Tiny,1=Short,2=Medium,3=Long,4=XL,5=Any
    int m_ratingTier  = 0;  // RatingTier: 0..4

    // ── UI refs ──
    std::vector<CCMenuItemSpriteExtra*> m_diffButtons;
    std::vector<CCMenuItemSpriteExtra*> m_demonButtons;
    std::vector<CCMenuItemSpriteExtra*> m_modeButtons;
    std::vector<CCMenuItemSpriteExtra*> m_lengthButtons;
    std::vector<cocos2d::CCSprite*>     m_ratingSprites; // one per tier, stacked
    cocos2d::CCNode*      m_demonRow      = nullptr;
    cocos2d::CCLabelBMFont* m_ratingName  = nullptr;
};

} // namespace paimon::foryou
