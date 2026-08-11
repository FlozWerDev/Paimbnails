#pragma once

#include <Geode/Geode.hpp>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace geode { class TextInput; }

namespace paimon::editorcp {

// Live eyedropper for the level editor. Samples one framebuffer pixel before
// swap; the HUD stays at the bottom so it cannot contaminate the sample.
// Click locks the color, Copy keeps the overlay open, Save applies it when a
// Color ID is set, and Cancel/Esc closes without committing.
class ColorPickerOverlay : public cocos2d::CCLayer {
public:
    static void show();
    static void hideOverlay();

    // Called by the pre-swap hook before the custom cursor is drawn.
    static void onPreSwapSample();

    bool init() override;
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void registerWithTouchDispatcher() override;
    void keyBackClicked() override;

    CREATE_FUNC(ColorPickerOverlay);

private:
    static ColorPickerOverlay* s_instance;

    std::vector<uint8_t>             m_pixelBuf;        // RGBA readback
    bool m_ready    = false;
    bool m_closing  = false;
    bool m_dragging = false;
    bool m_priorityScheduled = false;

    cocos2d::CCNode*      m_hud            = nullptr;
    cocos2d::CCMenu*      m_controlsMenu   = nullptr;
    cocos2d::CCNode*      m_swatchBox      = nullptr;
    cocos2d::CCLayerColor* m_selSwatch     = nullptr;
    cocos2d::CCLabelBMFont* m_valueLabel   = nullptr;
    cocos2d::CCLabelBMFont* m_formatLabel  = nullptr;
    cocos2d::CCLabelBMFont* m_swatchCaption = nullptr;
    geode::TextInput*     m_idInput        = nullptr;

    cocos2d::ccColor3B m_liveColor{255, 255, 255};
    cocos2d::ccColor3B m_selColor{255, 255, 255};
    bool m_hasSelection = false;
    int  m_formatIndex  = 0;
    bool m_autoApply    = false;

    cocos2d::ccColor3B m_lastApplied{0, 0, 0};
    bool m_hasApplied     = false;
    bool m_autoNoIdWarned = false;

    void buildUI();
    void liveSample();
    void updateReadout();
    void pickAt(cocos2d::CCPoint glPos);
    bool pointInHud(cocos2d::CCPoint p) const;

    void onPrevFormat(cocos2d::CCObject*);
    void onNextFormat(cocos2d::CCObject*);
    void onPrevColorID(cocos2d::CCObject*);
    void onNextColorID(cocos2d::CCObject*);
    void stepColorID(int delta);
    void onCopy(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onToggleAuto(cocos2d::CCObject*);
    void onNoop(cocos2d::CCObject*) {}
    void applyColorToChannel(cocos2d::ccColor3B col, int channelID);
    void tryAutoApply();

    std::string currentValueString() const;
    void doClose();
};

}
