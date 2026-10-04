#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../../editor-suite/EditorPopupKit.hpp"

namespace geode { class TextInput; }

namespace paimon::editorcp {

// live eyedropper: samples the framebuffer before swap, so the hud and the
// loupe are kept away from the sampled pixels.
class ColorPickerOverlay : public cocos2d::CCLayer {
public:
    static void show();

    // called by the pre-swap hook before the custom cursor is drawn.
    static void onPreSwapSample();
    static bool handleKey(geode::KeyboardInputData const& data);

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
    static constexpr int kLoupeCells = 9;
    static constexpr std::size_t kRecentMax = 8;

    static ColorPickerOverlay* s_instance;

    bool m_ready    = false;
    bool m_closing  = false;
    bool m_dragging = false;
    bool m_priorityScheduled = false;

    cocos2d::CCNode* m_hud = nullptr;
    cocos2d::CCMenu* m_controlsMenu = nullptr;
    cocos2d::CCNode* m_pickBox = nullptr;
    geode::NineSlice* m_liveSwatch = nullptr;
    geode::NineSlice* m_pickSwatch = nullptr;
    cocos2d::CCLabelBMFont* m_pickEmpty = nullptr;
    cocos2d::CCLabelBMFont* m_valueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_liveLabel = nullptr;
    cocos2d::CCLabelBMFont* m_channelName = nullptr;
    geode::NineSlice* m_channelSwatch = nullptr;
    geode::TextInput* m_idInput = nullptr;
    CCMenuItemSpriteExtra* m_dockUpBtn = nullptr;
    CCMenuItemSpriteExtra* m_dockDownBtn = nullptr;
    CCMenuItemToggler* m_loupeToggle = nullptr;
    CCMenuItemToggler* m_autoToggle = nullptr;
    std::vector<paimon::editor::kit::Pill> m_formatPills;
    std::vector<geode::NineSlice*> m_recentSlots;
    std::vector<CCMenuItemSpriteExtra*> m_recentButtons;

    cocos2d::CCNode* m_loupe = nullptr;
    cocos2d::CCDrawNode* m_loupeGrid = nullptr;
    cocos2d::CCLabelBMFont* m_loupeLabel = nullptr;
    std::array<uint8_t, kLoupeCells * kLoupeCells * 4> m_loupeBuf{};
    int m_loupeW = 0;
    int m_loupeH = 0;
    int m_loupeCX = 0;
    int m_loupeCY = 0;
    bool m_loupeDirty = false;

    cocos2d::ccColor3B m_liveColor{255, 255, 255};
    cocos2d::ccColor3B m_selColor{255, 255, 255};
    std::vector<cocos2d::ccColor3B> m_recent;
    std::string m_shownValue;
    std::string m_shownLive;
    bool m_hasSelection = false;
    int  m_formatIndex  = 0;
    bool m_autoApply    = false;
    bool m_loupeEnabled = true;
    bool m_dockTop      = false;

    cocos2d::ccColor3B m_lastApplied{0, 0, 0};
    bool m_hasApplied     = false;
    bool m_autoNoIdWarned = false;

    void buildUI();
    void buildHeader(float width, float height);
    void buildSwatchSection(cocos2d::CCRect const& area);
    void buildChannelSection(cocos2d::CCRect const& area);
    void buildActionSection(cocos2d::CCRect const& area);
    void buildLoupe();
    void placeHud(bool animate);

    void liveSample();
    void updateReadout();
    void updateLoupe();
    void refreshFormatPills();
    void refreshRecent();
    void refreshChannel();
    void pickAt();
    void selectColor(cocos2d::ccColor3B color);
    void pushRecent(cocos2d::ccColor3B color);
    bool pointInHud(cocos2d::CCPoint p) const;

    void stepFormat(int delta);
    void setFormat(int index);
    void stepColorID(int delta);
    int  currentColorID() const;
    void setLoupeEnabled(bool enabled);
    void setDockTop(bool top);
    void onCopy();
    void onApply();
    void applyColorToChannel(cocos2d::ccColor3B col, int channelID);
    void tryAutoApply();

    std::string currentValueString() const;
    void doClose();
};

}
