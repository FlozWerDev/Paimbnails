#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/DefaultInclude.hpp>

class WhitelistPopup : public geode::Popup {
protected:
    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCSize m_scrollViewSize = {0, 0};
    std::vector<std::string> m_users;

    bool init();
    void fetchWhitelist();
    void rebuildList();
    void onAdd(cocos2d::CCObject*);
    void onRemove(cocos2d::CCObject* sender);

public:
    static WhitelistPopup* create();
};
