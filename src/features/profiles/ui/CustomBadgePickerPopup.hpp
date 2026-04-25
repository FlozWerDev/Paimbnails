#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/function.hpp>
#include "../../../features/emotes/models/EmoteModels.hpp"
#include <string>
#include <vector>

// Selector de emote como badge personalizado
class CustomBadgePickerPopup : public geode::Popup {
public:
    using SelectCallback = geode::CopyableFunction<void(std::string const& emoteName)>;

    static CustomBadgePickerPopup* create(int accountID, std::string const& currentBadge = "");

    void setOnSelectCallback(SelectCallback cb) { m_onSelect = std::move(cb); }

protected:
    int m_accountID = 0;
    std::string m_currentBadge;
    SelectCallback m_onSelect;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_currentLabel = nullptr;

    bool init(int accountID, std::string const& currentBadge);
    void buildEmoteGrid(std::vector<paimon::emotes::EmoteInfo> const& emotes);
    void onEmoteSelected(cocos2d::CCObject* sender);
    void onClearBadge(cocos2d::CCObject*);
    void onSaveBadge(std::string const& emoteName);
};