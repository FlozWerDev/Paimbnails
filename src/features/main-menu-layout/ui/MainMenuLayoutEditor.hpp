#pragma once

#include "../services/MainMenuLayoutManager.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::menu_layout {

// Editor minimalista de layout. Funciona sobre cualquier CCNode raiz
// (MenuLayer, PauseLayer, LevelInfoLayer) reusando MainMenuLayoutManager
// para la persistencia. Interaccion directa: tocar = seleccionar,
// arrastrar = mover (con snap), una sola asa = escalar. Barra inferior
// con Cancelar / Reset seleccion / Reset todo / Ocultar / opacidad / Guardar.
class MainMenuLayoutEditor : public cocos2d::CCLayer {
public:
    static MainMenuLayoutEditor* create(cocos2d::CCNode* root);
    static MainMenuLayoutEditor* getActive();
    static bool isActive();
    static void open(cocos2d::CCNode* root);

    void saveAndClose();
    void cancelAndClose();
    cocos2d::CCNode* getTargetRoot() const;

    ~MainMenuLayoutEditor() override;

protected:
    bool init(cocos2d::CCNode* root);
    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void keyBackClicked() override;
    void keyDown(cocos2d::enumKeyCodes key, double p1) override;
    void update(float dt) override;

private:
    struct Item {
        EditableMenuButton target;
    };

    enum class DragMode { None, Move, Scale };

    void collectItems();
    void disableTargetMenus();
    void buildUI();
    void redraw();

    Item* selectedItem();
    void selectIndex(int index);
    cocos2d::CCRect itemRect(Item const& item) const;
    // itemRect con el margen del contorno: es donde se dibuja el grip.
    cocos2d::CCRect outlineRect(Item const& item) const;
    cocos2d::CCPoint gripPos(Item const& item) const;
    Item* findItemAt(cocos2d::CCPoint worldPos);
    bool isBackgroundItem(Item const& item) const;

    MenuButtonLayout* liveLayout(Item const& item);
    void applyLive(Item const& item);
    cocos2d::CCPoint snapWorld(Item const& item, cocos2d::CCPoint proposedWorld);
    void nudgeSelection(cocos2d::CCPoint deltaWorld);
    void scaleSelection(float factor);
    void resetItemToDefault(Item const& item);

    void pushHistory();
    void applyHistory(LayoutSnapshot const& snapshot);
    void undo();
    void redo();
    LayoutSnapshot buildSnapshot() const;

    void onSave(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onResetSelected(cocos2d::CCObject*);
    void onResetAll(cocos2d::CCObject*);
    void onToggleHidden(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onSavePreset(cocos2d::CCObject*);
    void onLoadPreset(cocos2d::CCObject*);
    void openPresetPicker(bool saveMode);
    void onToggleBar(cocos2d::CCObject*);

    geode::WeakRef<cocos2d::CCNode> m_root;
    std::vector<Item> m_items;
    std::unordered_map<std::string, MenuButtonLayout> m_live;
    std::unordered_map<std::string, MenuButtonLayout> m_initial;
    int m_selected = -1;

    std::vector<geode::Ref<cocos2d::CCMenu>> m_disabledMenus;

    cocos2d::CCDrawNode* m_highlights = nullptr;
    cocos2d::CCDrawNode* m_outline = nullptr;
    cocos2d::CCDrawNode* m_grip = nullptr;
    cocos2d::CCDrawNode* m_guideX = nullptr;
    cocos2d::CCDrawNode* m_guideY = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCMenu* m_bar = nullptr;
    cocos2d::CCNode* m_barContainer = nullptr;
    CCMenuItemSpriteExtra* m_collapseBtn = nullptr;
    cocos2d::CCSprite* m_collapseArrow = nullptr;
    bool m_collapsed = false;
    Slider* m_opacitySlider = nullptr;

    DragMode m_drag = DragMode::None;
    cocos2d::CCPoint m_touchStart = { 0.f, 0.f };
    cocos2d::CCPoint m_itemStartWorld = { 0.f, 0.f };
    cocos2d::CCPoint m_scaleFixedWorld = { 0.f, 0.f };
    float m_scaleStartDist = 1.f;
    float m_itemStartScale = 1.f;
    bool m_dragChanged = false;

    std::vector<LayoutSnapshot> m_history;
    std::size_t m_historyCursor = 0;
    bool m_applyingHistory = false;
};

} // namespace paimon::menu_layout
