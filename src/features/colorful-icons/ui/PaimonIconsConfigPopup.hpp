#pragma once
// Garage popup for Paimon Icons: master switch, live preview strip and three
// tabs (Colores / Candados / Donde). Each tab only shows the controls the
// selected mode or lock style actually uses.

#include <Geode/ui/Popup.hpp>

#include <array>
#include <vector>

namespace cocos2d { class CCMenu; class CCNode; class CCSprite; class CCLabelBMFont; }
class SimplePlayer;
class ButtonSprite;

namespace paimon::icons::ui {

class PaimonIconsConfigPopup : public geode::Popup {
public:
    static PaimonIconsConfigPopup* open();

protected:
    enum class Tab { Colors = 0, Locks = 1, Areas = 2 };

    // What a preview slot demonstrates.
    enum class SlotRole { Colored, Locked, Unobtainable };

    struct PreviewSlot {
        SimplePlayer* player     = nullptr;
        cocos2d::CCSprite* lock  = nullptr;
        SlotRole role            = SlotRole::Colored;
        int iconID               = 1;
        int displayIndex         = 0;
    };

    bool init();

    void buildHeader();   // master ON/OFF switch + reset button
    void buildPreview();  // panel background + "disabled" overlay
    void buildTabs();
    void buildIconMakerSection();  // banda inferior que abre el Creador de Iconos

    void switchTab(Tab tab);
    void rebuildPreviewSlots();  // slots depend on the active tab
    void rebuildSelector();      // mode/style selector + description
    void rebuildControls();      // contextual rows for the current selection
    void refreshPreview(float dt);

    cocos2d::CCMenu* m_tabMenu = nullptr;
    std::array<ButtonSprite*, 3> m_tabSprites{};
    cocos2d::CCNode* m_previewPanel = nullptr;
    cocos2d::CCNode* m_previewIcons = nullptr;
    cocos2d::CCLabelBMFont* m_disabledLabel = nullptr;
    cocos2d::CCNode* m_selectorArea = nullptr;
    cocos2d::CCNode* m_controls = nullptr;
    std::vector<PreviewSlot> m_slots;
    Tab m_tab = Tab::Colors;
};

}  // namespace paimon::icons::ui
