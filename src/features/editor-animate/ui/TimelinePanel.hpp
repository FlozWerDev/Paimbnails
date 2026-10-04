#pragma once

// docked timeline over the editor: header with the active clip, a film strip of
// frames and the tool row. swallows its own touches so nothing drops on canvas.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

class LevelEditorLayer;

namespace paimon::animate {

class TimelinePanel : public cocos2d::CCLayer {
public:
    static TimelinePanel* create(LevelEditorLayer* editor);
    static TimelinePanel* get();

    // mouse wheel over the strip scrolls frames instead of the editor.
    bool handleScroll(float y, float x);

private:
    enum class Grab { None, Move, Strip };

    bool init(LevelEditorLayer* editor);
    ~TimelinePanel() override;

    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void rebuild();
    void buildHeader(float y);
    void buildStrip();
    void buildTools(float y);
    void refreshDynamic();
    void refreshCells();
    void refreshInfo();

    float panelHeight() const;
    cocos2d::CCRect stripRect() const;
    float contentWidth() const;
    void clampScroll();
    void ensureVisible(int frame);
    int cellAt(float localX) const;
    void tapCell(int index);

    void deleteCurrentFrame();
    void bakeActive();
    void cycleMode();
    void cycleGhosts();
    void toggleCollapsed();

    void tick(float dt);
    void applyVisibility();
    void loadPosition();
    void savePosition();
    void clampToScreen();

    static TimelinePanel* s_instance;

    LevelEditorLayer* m_editor = nullptr;
    bool m_collapsed = false;
    float m_scroll = 0.f;
    float m_infoWidth = 80.f;

    int m_shownRevision = -1;
    int m_cellsRevision = -1;
    int m_shownFrame = -1;
    int m_shownHead = -1;
    bool m_shownPlaying = false;

    Grab m_grab = Grab::None;
    cocos2d::CCPoint m_grabOffset{};
    float m_grabX = 0.f;
    float m_scrollStart = 0.f;
    bool m_dragged = false;
    cocos2d::CCSize m_lastWin{};

    cocos2d::CCNode* m_root = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCNode* m_cells = nullptr;
    cocos2d::CCLabelBMFont* m_infoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_timeLabel = nullptr;
    CCMenuItemSpriteExtra* m_playButton = nullptr;
};

} // namespace paimon::animate
