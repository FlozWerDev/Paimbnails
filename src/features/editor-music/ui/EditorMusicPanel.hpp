#pragma once

// Pinned native-GD floating music widget for the level editor (Ctrl+M).
// Swallows its own touches, morphs smoothly between Full Deck and Mini-Bar,
// spins album art/vinyl with live FMOD metering, and hides during playtest.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>

class LevelEditorLayer;

namespace paimon::editormusic {

class EditorMusicPanel : public cocos2d::CCLayer {
public:
    using cocos2d::CCLayer::init;
    static EditorMusicPanel* create(LevelEditorLayer* editor);
    static EditorMusicPanel* get();

    // Toggles open/close with animation. Returns the new open state.
    bool toggleOpen();
    bool isOpen() const { return m_open; }

    void refreshTrackInfo(bool animateChange = false);
    void refreshSkin();
    void resetPosition();
    void applyVisibilityImmediate();

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
    void rebuildSkinBackdrop();
    CCMenuItemSpriteExtra* iconButton(
        std::initializer_list<char const*> frames,
        float size,
        std::function<void()> onPress
    );

    void tick(float dt);
    void updateAnimations(float dt);
    void applyPanelLayoutAndOpacity();
    void updateTransport();
    void updateBars(float dt);
    void updateMarquee(float dt);
    void updateEqualizer(float dt);
    void updateArtwork(float dt);
    void loadCoverTexture(std::string const& path, int songId);
    void applyCoverTexture(cocos2d::CCTexture2D* tex);
    void triggerButtonPop(cocos2d::CCNode* node, float peakScale = 1.24f);

    void openPicker(int initialTab = 0);
    void applySeekAt(cocos2d::CCPoint local);
    void applyVolumeAt(cocos2d::CCPoint local);

    void loadPosition();
    void savePosition();
    void clampToScreen();

    static EditorMusicPanel* s_instance;

    LevelEditorLayer* m_editor = nullptr;
    bool m_open = false;
    bool m_collapsed = false;

    // Animated normalized states (0..1)
    float m_openAnim = 0.f;
    float m_collapseAnim = 0.f;
    float m_playtestAnim = 1.f;
    float m_trackSlideAnim = 1.f;
    float m_clock = 0.f;
    float m_spinAngle = 0.f;
    float m_spinVel = 0.f;
    float m_marqueeOffset = 0.f;
    float m_marqueePause = 1.0f;
    int m_marqueeDir = -1;
    float m_audioEnergy = 0.f;
    std::array<float, 5> m_eqLevels = {0.18f, 0.28f, 0.35f, 0.24f, 0.16f};

    // Native GD frame & chrome
    geode::NineSlice* m_panelBg = nullptr;
    geode::NineSlice* m_innerWell = nullptr;
    std::array<cocos2d::CCSprite*, 2> m_corners = {};
    int m_appliedSkin = -1;

    // Artwork slot (left)
    cocos2d::CCMenu* m_artMenu = nullptr;
    CCMenuItemSpriteExtra* m_artBtn = nullptr;
    geode::NineSlice* m_artFrame = nullptr;
    cocos2d::CCNode* m_discRotor = nullptr;
    cocos2d::CCClippingNode* m_artClip = nullptr;
    cocos2d::CCSprite* m_artCoverSprite = nullptr;
    cocos2d::CCSprite* m_artFallbackIcon = nullptr;
    std::string m_loadedCoverPath;
    std::uint64_t m_coverGen = 0;

    // Title & subtitle (clipped for smooth marquee & track slide)
    cocos2d::CCNode* m_titleClipStencil = nullptr;
    cocos2d::CCClippingNode* m_titleClip = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_subtitleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_timeLabel = nullptr;
    float m_titleClipW = 132.f;

    // Equalizer (native square02b_001 bars)
    std::array<geode::NineSlice*, 5> m_eqTracks = {};
    std::array<geode::NineSlice*, 5> m_eqBars = {};

    // Native GD seek & volume bars (GJ_progressBar_001 + sliderthumb)
    cocos2d::extension::CCScale9Sprite* m_seekGroove = nullptr;
    cocos2d::extension::CCScale9Sprite* m_seekFill = nullptr;
    cocos2d::CCSprite* m_seekThumb = nullptr;
    float m_seekBarArtH = 20.f;

    cocos2d::extension::CCScale9Sprite* m_volGroove = nullptr;
    cocos2d::extension::CCScale9Sprite* m_volFill = nullptr;
    cocos2d::CCSprite* m_volThumb = nullptr;

    // Menus & buttons
    cocos2d::CCMenu* m_headerMenu = nullptr;
    cocos2d::CCMenu* m_deckMenu = nullptr;
    cocos2d::CCMenu* m_miniMenu = nullptr;

    CCMenuItemSpriteExtra* m_collapseBtn = nullptr;
    CCMenuItemSpriteExtra* m_menuBtn = nullptr;
    CCMenuItemSpriteExtra* m_fxBtn = nullptr;
    CCMenuItemSpriteExtra* m_closeBtn = nullptr;

    CCMenuItemSpriteExtra* m_favBtn = nullptr;
    cocos2d::CCSprite* m_favIcon = nullptr;
    CCMenuItemSpriteExtra* m_playBtn = nullptr;
    cocos2d::CCSprite* m_shuffleIcon = nullptr;
    cocos2d::CCSprite* m_repeatIcon = nullptr;
    cocos2d::CCLabelBMFont* m_repeatBadge = nullptr;
    CCMenuItemSpriteExtra* m_muteBtn = nullptr;
    cocos2d::CCSprite* m_muteIcon = nullptr;

    CCMenuItemSpriteExtra* m_miniPlayBtn = nullptr;
    CCMenuItemSpriteExtra* m_miniNextBtn = nullptr;
    CCMenuItemSpriteExtra* m_miniExpandBtn = nullptr;
    CCMenuItemSpriteExtra* m_miniCloseBtn = nullptr;

    Grab m_grab = Grab::None;
    cocos2d::CCPoint m_grabOffset{};
    float m_seekPreviewRatio = 0.f;
    bool m_seekFillVisible = false;
    bool m_volFillVisible = false;
    int m_lastTimePosSec = -1;
    int m_lastTimeLenSec = -1;
    cocos2d::CCSize m_lastWin{};
    std::string m_shownTrackId;
    bool m_shownPlaying = false;
    bool m_shownMiniPlaying = false;
    bool m_shownFav = false;
    bool m_shownMuted = false;
    bool m_firstSyncDone = false;
    std::size_t m_playerListenerToken = 0;
};

} // namespace paimon::editormusic
