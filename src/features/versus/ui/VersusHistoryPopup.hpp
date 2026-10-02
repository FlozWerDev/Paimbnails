#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../data/VersusTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <vector>

namespace paimon::versus {

// the last twenty duels, straight from the local cache: the server has more,
// but this is the list you want the instant the popup opens.
class VersusHistoryPopup : public PaimonPopup {
public:
    static VersusHistoryPopup* create();

protected:
    bool init() override;
    void buildRows();
    void onMode(cocos2d::CCObject* sender);

    Mode m_mode = Mode::Classic;
    geode::ScrollLayer* m_scroll = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_modeButtons;
};

} // namespace paimon::versus
