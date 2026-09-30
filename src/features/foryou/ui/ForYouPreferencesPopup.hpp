#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <vector>

namespace paimon::foryou {

// quality tier used by the cycling rating button (bottom-left of the popup).
// each tier is a superset of the previous one when seeded into the tracker.
enum class RatingTier : int {
    StarRated = 0,
    Featured  = 1,
    Epic      = 2,
    Legendary = 3,
    Mythic    = 4,
    Count     = 5
};

class ForYouPreferencesPopup : public geode::Popup {
public:
    static ForYouPreferencesPopup* create(std::function<void()> onConfirm);

protected:
    bool init(std::function<void()> onConfirm);

    void onDifficultySelect(cocos2d::CCObject* sender);
    void onDemonDiffSelect(cocos2d::CCObject* sender);
    void onGameModeSelect(cocos2d::CCObject* sender);
    void onLengthSelect(cocos2d::CCObject* sender);
    void onRatingCycle(cocos2d::CCObject* sender);
    void onTagPreferences(cocos2d::CCObject* sender);
    void onConfirm(cocos2d::CCObject* sender);

    void refreshDifficultyButtons();
    void refreshDemonButtons();
    void refreshGameModeButtons();
    void refreshLengthButtons();
    void refreshRatingTier();
    void refreshDemonRowVisibility();

    std::function<void()> m_onConfirm;
    int m_difficulty  = 30; // 10=easy,20=normal,30=hard,40=harder,50=insane,60=demon
    int m_demonDiff   = 0;  // 0=any,1=easy,2=medium,3=hard,4=insane,5=extreme
    int m_gameMode    = 0;  // 0=classic,1=platformer,2=both
    int m_length      = 2;  // 0=tiny,1=short,2=medium,3=long,4=xl,5=any
    int m_ratingTier  = 0;  // ratingtier: 0..4

    std::vector<CCMenuItemSpriteExtra*> m_diffButtons;
    std::vector<CCMenuItemSpriteExtra*> m_demonButtons;
    std::vector<CCMenuItemSpriteExtra*> m_modeButtons;
    std::vector<CCMenuItemSpriteExtra*> m_lengthButtons;
    std::vector<cocos2d::CCSprite*>     m_ratingSprites; // one per tier, stacked
    cocos2d::CCNode*      m_demonRow      = nullptr;
    // fills the demon row's slot while a non-demon difficulty is selected.
    cocos2d::CCLabelBMFont* m_demonHint   = nullptr;
    cocos2d::CCLabelBMFont* m_ratingName  = nullptr;
};

} // namespace paimon::foryou
