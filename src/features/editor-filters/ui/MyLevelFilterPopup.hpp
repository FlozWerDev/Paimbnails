#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../services/MyLevelFilters.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <map>
#include <vector>

namespace paimon::editorfilters {

class MyLevelFilterPopup : public PaimonPopup {
protected:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

    void onLengthChip(cocos2d::CCObject* sender);
    void onStatusChip(cocos2d::CCObject* sender);
    void onFlagChip(cocos2d::CCObject* sender);
    void onSortPrev(cocos2d::CCObject* sender);
    void onSortNext(cocos2d::CCObject* sender);
    void onReset(cocos2d::CCObject* sender);
    void onApply(cocos2d::CCObject* sender);

    CCMenuItemSpriteExtra* makeChip(char const* text, int tag, cocos2d::SEL_MenuHandler sel);
    void refreshChips();
    void refreshCount();
    void syncFromInputs();
    void reloadBrowser();

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_songInput = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    geode::TextInput* m_minObjInput = nullptr;
    geode::TextInput* m_maxObjInput = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sortLabel = nullptr;

    std::map<int, CCMenuItemSpriteExtra*> m_lengthChips;
    std::map<int, CCMenuItemSpriteExtra*> m_statusChips;
    std::map<int, CCMenuItemSpriteExtra*> m_flagChips;

public:
    static MyLevelFilterPopup* create();
};

} // namespace paimon::editorfilters
