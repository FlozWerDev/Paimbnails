#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

class PaimonLoadingOverlay;
#include <Geode/utils/function.hpp>
#include <Geode/ui/ScrollLayer.hpp>

class AddModeratorPopup : public geode::Popup {
protected:
    geode::TextInput* m_usernameInput = nullptr;
    PaimonLoadingOverlay* m_loadingSpinner = nullptr;
    geode::CopyableFunction<void(bool, std::string const&)> m_callback;

    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCSize m_scrollViewSize = {0, 0};
    std::vector<std::string> m_moderatorNames;
    
    bool init(geode::CopyableFunction<void(bool, std::string const&)> callback);
    void onAdd(cocos2d::CCObject*);
    void onRemove(cocos2d::CCObject* sender);
    void fetchAndShowModerators();
    void rebuildList();
    
public:
    static AddModeratorPopup* create(geode::CopyableFunction<void(bool, std::string const&)> callback);
};
