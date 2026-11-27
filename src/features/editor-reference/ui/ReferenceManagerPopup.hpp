#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include "../../editor-suite/EditorPopupKit.hpp"
#include "../ReferenceTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>

namespace paimon::editorref {

class ReferenceManagerPopup : public PaimonPopup {
public:
    static ReferenceManagerPopup* create(int initialSelectedId = 0);

private:
    enum class Tab {
        Level,
        History
    };

    bool init(int initialSelectedId);
    void onClose(cocos2d::CCObject* sender) override;

    void setTab(Tab tab);
    void refreshTabBar();
    void refreshAll();
    void rebuildList();
    void rebuildInspector();
    void rebuildHistory();
    void refreshPreview();
    void pickNewImage();
    void pasteClipboardImage();
    void useHistoryEntry(std::string const& historyId);
    void deleteHistoryEntry(std::string const& historyId);
    void confirmClearHistory();
    void onOpacitySliderChanged(cocos2d::CCObject* sender);

    Tab m_tab = Tab::Level;
    int m_selectedId = 0;

    editor::kit::Pill m_levelTabPill;
    editor::kit::Pill m_historyTabPill;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_tipLabel = nullptr;

    cocos2d::CCNode* m_levelView = nullptr;
    cocos2d::CCNode* m_listContainer = nullptr;
    cocos2d::CCNode* m_inspectorContainer = nullptr;

    cocos2d::CCNode* m_historyView = nullptr;
    cocos2d::CCNode* m_historyContainer = nullptr;

    // Live inspector references (rebuilt on selection change).
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_opacityValueLabel = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    editor::kit::Pill m_layerPill;
    editor::kit::Pill m_lockPill;
    editor::kit::Pill m_visiblePill;
    editor::kit::Pill m_playtestPill;
};

} // namespace paimon::editorref
