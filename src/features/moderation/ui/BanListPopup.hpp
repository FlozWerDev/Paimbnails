#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/DefaultInclude.hpp>

struct BanDetail {
    std::string reason;
    std::string bannedBy;
    std::string date;
};

class BanListPopup : public geode::Popup {
protected:
    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCSize m_scrollViewSize = {0, 0};
    std::map<std::string, BanDetail> m_banDetails;

    bool init();
    void rebuildList(std::vector<std::string> const& users);
    void onUnban(cocos2d::CCObject* sender);
    void onInfo(cocos2d::CCObject* sender);

public:
    static BanListPopup* create();
};
