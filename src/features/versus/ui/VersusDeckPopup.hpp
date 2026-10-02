#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../data/VersusCards.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <vector>

namespace paimon::versus {

// the deck browser, which doubles as the place you learn the mode: every card
// with its full text, filtered by rarity.
class VersusDeckPopup : public PaimonPopup {
public:
    static VersusDeckPopup* create();

protected:
    bool init() override;

    void buildFilters();
    void rebuildGrid();
    void onFilter(cocos2d::CCObject* sender);

    int m_filter = -1;   // -1 is every rarity
    geode::ScrollLayer* m_scroll = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_filterButtons;
};

} // namespace paimon::versus
