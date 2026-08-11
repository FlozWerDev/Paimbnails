#pragma once

// Two small popups over the same list: picking a saved search to load, and
// naming the current one to save it.

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../services/AdvancedSearch.hpp"
#include <functional>

namespace paimon::info {

class SearchPresetsPopup : public geode::Popup {
public:
    static SearchPresetsPopup* createPicker(std::function<void(AdvancedQuery const&)> onPick);
    static SearchPresetsPopup* createSaveDialog(AdvancedQuery query,
                                                std::function<void()> onSaved);

protected:
    bool init(bool saveMode, AdvancedQuery query,
              std::function<void(AdvancedQuery const&)> onPick,
              std::function<void()> onSaved);
    void onClose(cocos2d::CCObject* sender) override;

    void rebuildList();
    void onPick(cocos2d::CCObject* sender);
    void onDelete(cocos2d::CCObject* sender);
    void onSave(cocos2d::CCObject*);

    bool m_saveMode = false;
    AdvancedQuery m_query;
    std::function<void(AdvancedQuery const&)> m_onPick;
    std::function<void()> m_onSaved;

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
};

} // namespace paimon::info
