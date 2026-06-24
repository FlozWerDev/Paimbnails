#pragma once

#include <Geode/Geode.hpp>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace geode { class TextInput; }

namespace paimon::editorcp {

// Full-screen eyedropper overlay used inside the level editor.
//
// Flow (live eyedropper, no magnifier):
//   1. On show() the overlay opens transparently — the editor keeps rendering
//      underneath, so the picked colors track the scene in real time.
//   2. The HUD swatch shows the LIVE color under the cursor in real time.
//      The value label updates every frame too.
//   3. Clicking locks in the current live color as the selection ("PICKED").
//   4. Copy  -> copies the value to the clipboard (stays open).
//      Save  -> copies + (if a Color ID is set) opens GD's native color popup
//               for that channel seeded with the picked color, then closes.
//      Cancel/Esc -> closes without committing.
//
// Real-time sampling: every frame, just before the buffer swap, the single
// pixel under the cursor is read back from the framebuffer (see onPreSwapSample).
// The HUD lives at the bottom so it never contaminates the sampled pixel.
class ColorPickerOverlay : public cocos2d::CCLayer {
public:
    static void show();        // toggles: opens, or closes if already open
    static void hideOverlay(); // closes if open

    // Called from the CCEGLView swap-buffers hook (pre-swap, before the custom
    // cursor is drawn) to sample the live framebuffer around the cursor.
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

    // Live framebuffer sampling
    std::vector<uint8_t>             m_pixelBuf;        // RGBA readback for 1 pixel
    bool m_ready    = false;
    bool m_closing  = false;
    bool m_dragging = false;
    bool m_priorityScheduled = false; // menu touch priority deferred once

    // Scene nodes
    cocos2d::CCNode*      m_hud            = nullptr;
    cocos2d::CCMenu*      m_controlsMenu   = nullptr;
    cocos2d::CCNode*      m_swatchBox      = nullptr; // decorative frame + clip (pop target)
    cocos2d::CCLayerColor* m_selSwatch     = nullptr;
    cocos2d::CCLabelBMFont* m_valueLabel   = nullptr;
    cocos2d::CCLabelBMFont* m_formatLabel  = nullptr;
    cocos2d::CCLabelBMFont* m_swatchCaption = nullptr; // "LIVE" / "PICKED"
    geode::TextInput*     m_idInput        = nullptr;

    // State
    cocos2d::ccColor3B m_liveColor{255, 255, 255};
    cocos2d::ccColor3B m_selColor{255, 255, 255};
    bool m_hasSelection = false;
    int  m_formatIndex  = 0;
    bool m_autoApply    = false;

    // Auto-apply bookkeeping
    cocos2d::ccColor3B m_lastApplied{0, 0, 0};
    bool m_hasApplied     = false; // a color was pushed to the channel at least once
    bool m_autoNoIdWarned = false; // "enter a Color ID" hint shown once this session

    void buildUI();
    void liveSample();         // pre-swap: read the pixel under the cursor (GL)
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
    void tryAutoApply(); // push the current selection to the Color ID channel (live)

    std::string currentValueString() const;
    void doClose();
};

} // namespace paimon::editorcp
