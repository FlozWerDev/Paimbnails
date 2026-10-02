#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

class PaimonLoadingOverlay;

// admin-only role manager; users resolve to a real gd accountid before upload.
// class name kept for the existing profilepage call site.
class AddModeratorPopup : public PaimonPopup {
protected:
    geode::TextInput* m_usernameInput = nullptr;
    PaimonLoadingOverlay* m_loadingSpinner = nullptr;
    geode::CopyableFunction<void(bool, std::string const&)> m_callback;

    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCLabelBMFont* m_addLabel = nullptr;
    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCSize m_scrollViewSize = {0, 0};

    std::string m_activeRole = "mod";
    std::vector<std::string> m_members;

    bool init(geode::CopyableFunction<void(bool, std::string const&)> callback);
    void buildRoleTabs();
    void selectRole(std::string const& role);
    void onRoleTab(cocos2d::CCObject* sender);
    void refreshAddLabel();
    void onAdd(cocos2d::CCObject*);
    void onRemove(cocos2d::CCObject* sender);
    void fetchMembers();
    void rebuildList();
    std::string roleDisplayName(std::string const& role) const;

public:
    static AddModeratorPopup* create(geode::CopyableFunction<void(bool, std::string const&)> callback);
};
