#pragma once

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include "../core/UIConstants.hpp"

// overlay fullscreen pa editar pos/scale/opacity de botones. aceptar, reiniciar, seleccion, sliders, highlight

struct EditableButton {
    cocos2d::CCMenuItem* item = nullptr;
    std::string buttonID;
    std::string highlightKey;
    cocos2d::CCPoint originalPos;
    float originalScale = 1.0f;
    float originalOpacity = 1.0f;
};

class ButtonEditOverlay : public cocos2d::CCLayer {
protected:
    std::string m_sceneKey;
    geode::Ref<cocos2d::CCMenu> m_targetMenu;
    std::vector<geode::Ref<cocos2d::CCMenu>> m_extraMenus;
    std::vector<geode::Ref<cocos2d::CCMenu>> m_disabledMenus;
    std::vector<EditableButton> m_editableButtons;
    EditableButton* m_selectedButton = nullptr;
    bool m_isClosing = false;
    
    cocos2d::CCLayerColor* m_darkBG = nullptr;
    geode::Ref<cocos2d::CCDrawNode> m_selectionHighlight;
    std::unordered_map<std::string, cocos2d::CCDrawNode*> m_buttonHighlights;
    
    cocos2d::CCMenu* m_controlsMenu = nullptr;
    Slider* m_scaleSlider = nullptr;
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;

    // drag state
    EditableButton* m_draggedButton = nullptr;
    cocos2d::CCPoint m_dragStartPos;
    cocos2d::CCPoint m_originalButtonPos;

    // snap/alinear
    float m_snapThreshold = paimon::ui::constants::editor::SNAP_THRESHOLD;
    cocos2d::CCDrawNode* m_snapGuideX = nullptr; // guia X
    cocos2d::CCDrawNode* m_snapGuideY = nullptr; // guia Y
    bool m_snappedX = false;
    bool m_snappedY = false;

    bool init(std::string const& sceneKey, cocos2d::CCMenu* menu, std::vector<cocos2d::CCMenu*> const& extraMenus = {});
    
    void collectEditableButtons();
    void disableOtherMenus(cocos2d::CCNode* root);
    void createControls();
    void selectButton(EditableButton* btn);
    void updateSelectionHighlight();
    void createAllHighlights();
    void updateAllHighlights();
    void clearAllHighlights();
    void drawRoundedRect(cocos2d::CCDrawNode* node, float w, float h, cocos2d::ccColor4F fill);
    void updateSliderLabels();
    void showControls(bool show);
    void update(float dt) override;
    
    // touch pa arrastrar
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    
    EditableButton* findButtonAtPoint(cocos2d::CCPoint worldPos);
    bool isTouchOnSlider(cocos2d::CCTouch* touch);

    // snap
    cocos2d::CCPoint applySnap(cocos2d::CCPoint pos);
    void createSnapGuides();
    void updateSnapGuides(bool showX, bool showY, float snapX, float snapY);
    void hideSnapGuides();

    void onAccept(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
    void onScaleChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);

public:
    static ButtonEditOverlay* create(std::string const& sceneKey, cocos2d::CCMenu* menu,
                                     std::vector<cocos2d::CCMenu*> const& extraMenus = {});
    ~ButtonEditOverlay() override;
};

