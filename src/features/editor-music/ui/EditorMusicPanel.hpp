#pragma once

// Compact music widget pinned to the top left of the editor. Ctrl+M toggles it.
// It handles its own touches so clicks on the panel never drop objects on the
// canvas behind it, and it hides itself while the editor UI is hidden or a
// playtest is running.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <functional>
#include <initializer_list>
#include <string>

class LevelEditorLayer;

namespace paimon::editormusic {

class EditorMusicPanel : public cocos2d::CCLayer {
public:
    static EditorMusicPanel* create(LevelEditorLayer* editor);
    static EditorMusicPanel* get();

    // Ctrl+M. Returns the new open state.
    bool toggleOpen();
    bool isOpen() const { return m_open; }

    void refreshTrackInfo();

private:
    enum class Grab { None, Move, Seek, Volume };

    bool init(LevelEditorLayer* editor);
    ~EditorMusicPanel() override;

    void onExit() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void buildUI();
    CCMenuItemSpriteExtra* iconButton(
        std::initializer_list<char const*> frames, float size, std::function<void()> onPress);

    void tick(float dt);
    void applyVisibility();
    void updateTransport();
    void updateBars();

    void openPicker();
    void applySeekAt(cocos2d::CCPoint local);
    void applyVolumeAt(cocos2d::CCPoint local);

    void loadPosition();
    void savePosition();
    void clampToScreen();

    static EditorMusicPanel* s_instance;

    LevelEditorLayer* m_editor = nullptr;
    bool m_open = false;

    cocos2d::CCMenu* m_menu = nullptr;
    CCMenuItemSpriteExtra* m_playButton = nullptr;
    cocos2d::CCSprite* m_shuffleIcon = nullptr;
    cocos2d::CCSprite* m_repeatIcon = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_timeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_repeatBadge = nullptr;
    cocos2d::CCLayerColor* m_seekFill = nullptr;
    cocos2d::CCLayerColor* m_volumeFill = nullptr;

    Grab m_grab = Grab::None;
    cocos2d::CCPoint m_grabOffset{};
    cocos2d::CCSize m_lastWin{};
    std::string m_shownTrackId;
    bool m_shownPlaying = false;
};

} // namespace paimon::editormusic
