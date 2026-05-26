#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include "../core/UIConstants.hpp"

// overlay pantalla completa para pos/escala/opacidad de botones y etiquetas (capas de edicion separadas)

enum class ButtonEditFacet : int { Buttons = 0, Labels = 1 };

struct ButtonEditEntry {
    cocos2d::CCNode* node = nullptr;
    std::string layoutId;
    std::string highlightKey;
    cocos2d::CCPoint originalPos;
    float originalScale = 1.0f;
    float originalOpacity = 1.0f;
    int originalZOrder = 0;
};

class ButtonEditOverlay : public cocos2d::CCLayer {
protected:
    std::string m_sceneKey;
    geode::Ref<cocos2d::CCMenu> m_targetMenu;
    std::vector<geode::Ref<cocos2d::CCMenu>> m_extraMenus;
    geode::Ref<cocos2d::CCNode> m_labelScanRoot;
    std::vector<geode::Ref<cocos2d::CCMenu>> m_disabledMenus;

    std::vector<ButtonEditEntry> m_buttonEntries;
    std::vector<ButtonEditEntry> m_labelEntries;
    ButtonEditFacet m_facet = ButtonEditFacet::Buttons;

    ButtonEditEntry* m_selectedEntry = nullptr;
    bool m_isClosing = false;

    cocos2d::CCLayerColor* m_darkBG = nullptr;
    geode::Ref<cocos2d::CCDrawNode> m_selectionHighlight;
    std::unordered_map<std::string, cocos2d::CCDrawNode*> m_buttonHighlights;

    cocos2d::CCMenu* m_controlsMenu = nullptr;
    cocos2d::CCMenu* m_facetMenu = nullptr;
    CCMenuItemSpriteExtra* m_tabButtons = nullptr;
    CCMenuItemSpriteExtra* m_tabLabels = nullptr;
    Slider* m_scaleSlider = nullptr;
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;
    cocos2d::CCLabelBMFont* m_panelTitleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_facetBannerLabel = nullptr;
    cocos2d::CCLabelBMFont* m_instructionLabel = nullptr;

    // drag
    ButtonEditEntry* m_draggedEntry = nullptr;
    cocos2d::CCPoint m_dragStartPos;
    cocos2d::CCPoint m_originalNodePos;

    // snap
    float m_snapThreshold = paimon::ui::constants::editor::SNAP_THRESHOLD;
    cocos2d::CCDrawNode* m_snapGuideX = nullptr;
    cocos2d::CCDrawNode* m_snapGuideY = nullptr;
    bool m_snappedX = false;
    bool m_snappedY = false;

    std::vector<ButtonEditEntry>* activeEntries();
    std::vector<ButtonEditEntry> const* activeEntries() const;
    void collectButtonEntries();
    void collectLabelEntries();
    void setFacet(ButtonEditFacet facet);
    void updateFacetUI();
    void disableOtherMenus(cocos2d::CCNode* root);
    void createControls();
    void onFacetTab(cocos2d::CCObject* sender);

    void selectEntry(ButtonEditEntry* entry);
    void updateSelectionHighlight();
    void createAllHighlights();
    void updateAllHighlights();
    void clearAllHighlights();
    void drawRoundedRect(cocos2d::CCDrawNode* node, float w, float h, cocos2d::ccColor4F fill);
    void updateSliderLabels();
    void showControls(bool show);
    void update(float dt) override;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    ButtonEditEntry* findEntryAtPoint(cocos2d::CCPoint worldPos);
    bool isTouchOnSlider(cocos2d::CCTouch* touch);

    cocos2d::CCPoint applySnap(cocos2d::CCPoint pos);
    void createSnapGuides();
    void updateSnapGuides(bool showX, bool showY, float snapX, float snapY);
    void hideSnapGuides();

    void onAccept(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
    void onScaleChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);

    bool init(std::string const& sceneKey, cocos2d::CCMenu* menu,
        std::vector<cocos2d::CCMenu*> const& extraMenus, cocos2d::CCNode* labelScanRoot);

public:
    static ButtonEditOverlay* create(std::string const& sceneKey, cocos2d::CCMenu* menu,
        std::vector<cocos2d::CCMenu*> const& extraMenus = {}, cocos2d::CCNode* labelScanRoot = nullptr);
    ~ButtonEditOverlay() override;
};
