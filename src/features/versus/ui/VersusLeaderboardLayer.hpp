#pragma once

#include "../data/VersusTypes.hpp"
#include "../services/VersusClient.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <string>
#include <vector>

namespace paimon::versus {

class VersusLeaderboardLayer : public cocos2d::CCLayer {
public:
    static VersusLeaderboardLayer* create(Mode mode);
    static cocos2d::CCScene* scene(Mode mode);

protected:
    bool init(Mode mode);
    void keyBackClicked() override;

    void buildChrome();
    void buildTabs();
    void load();
    void buildRows();
    void setStatus(std::string const& text);

    void onBack(cocos2d::CCObject* sender);
    void onMode(cocos2d::CCObject* sender);
    void onScope(cocos2d::CCObject* sender);

    Mode m_mode = Mode::Classic;
    std::string m_scope = "global";
    // only the newest request may paint; the rest answer into a tab that has
    // already been left.
    uint32_t m_request = 0;
    std::vector<LeaderboardRow> m_rows;

    cocos2d::CCMenu* m_menu = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_modeButtons;
    std::vector<CCMenuItemSpriteExtra*> m_scopeButtons;
};

} // namespace paimon::versus
