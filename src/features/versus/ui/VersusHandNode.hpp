#pragma once

#include "../data/VersusCards.hpp"
#include "../services/VersusEffects.hpp"

#include <Geode/Geode.hpp>

#include <vector>

namespace paimon::versus {

// The two card slots and the row of active effects, bottom right of the level.
// It reads the session every frame rather than being told, so a card dealt by
// crossing a milestone shows up without anything having to call in.
class VersusHandNode : public cocos2d::CCNode {
public:
    static VersusHandNode* create();

    void refresh();

protected:
    bool init() override;
    void update(float dt) override;
    void rebuildHand();
    void rebuildRivalHand();
    // Touch/click path for playing a card (mobile has no Q/E keybinds).
    void onPlayCard(cocos2d::CCObject* sender);
    // Takes the list refresh() already holds, so the rings line up with the
    // glyphs they were built from.
    void rebuildEffects(std::vector<ActiveEffect> const& active);

    std::vector<CardId> m_drawn;
    std::vector<CardId> m_drawnRival;
    std::vector<ActiveEffect> m_drawnEffects;
    cocos2d::CCNode* m_slots = nullptr;
    cocos2d::CCNode* m_rivalSlots = nullptr;
    cocos2d::CCNode* m_effects = nullptr;
    cocos2d::CCLabelBMFont* m_locked = nullptr;
    // One entry per active effect, null for the ones with no clock.
    std::vector<cocos2d::CCProgressTimer*> m_rings;
};

} // namespace paimon::versus
